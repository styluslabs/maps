#include "offlinemaps.h"
#include "mapsapp.h"
#include "mapsearch.h"
#include "util.h"
//#include "imgui.h"
#include <deque>
//#include "sqlite3/sqlite3.h"
#include "sqlitepp.h"
// "private" headers
#include "scene/scene.h"
#include "data/mbtilesDataSource.h"
#include "data/networkDataSource.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"

#include "mapsources.h"
#include "mapwidgets.h"

static bool runOfflineWorker = false;
static std::unique_ptr<std::thread> offlineWorker;
static Semaphore semOfflineWorker(1);
static const char* polylineStyle = "{ style: lines, color: red, width: 4px, order: 5000 }";  //interactive: true,


// Offline maps
// - initial discussion https://github.com/tangrams/tangram-es/issues/931

struct OfflineSourceInfo
{
  std::string name;
  std::string cacheFile;
  std::string url;
  Tangram::UrlOptions urlOptions;
  int maxZoom;
  YAML::Node searchData;
};

struct OfflineMapInfo
{
  int id;
  LngLat lngLat00, lngLat11;
  int zoom, maxZoom;
  std::vector<OfflineSourceInfo> sources;
  bool canceled;
};

class OfflineDownloader
{
public:
    OfflineDownloader(Platform& _platform, const OfflineMapInfo& ofl, const OfflineSourceInfo& src);
    ~OfflineDownloader();
    size_t remainingTiles() const { return m_queued.size() + m_pending.size(); }
    bool fetchNextTile(int maxPending);
    void cancel();
    std::string name;
    int totalTiles = 0;
    int maxRetries = 4;

private:
    void tileTaskCallback(std::shared_ptr<TileTask> _task);

    int offlineId;
    int srcMaxZoom;
    int64_t offlineSize;
    bool canceled = false;
    std::deque<TileID> m_queued;
    std::vector<TileID> m_pending;
    std::mutex m_mutexQueue;
    std::unique_ptr<Tangram::MBTilesDataSource> mbtiles;
    std::vector<SearchData> searchData;
};

static MapsOffline* mapsOfflineInst = NULL;  // for updateProgress()
static int maxOfflineDownloads = 4;
static std::atomic<Timestamp> prevProgressUpdate(0);
static ThreadSafeQueue<OfflineMapInfo, std::list<OfflineMapInfo>> offlinePending;
static ThreadSafeQueue<std::unique_ptr<OfflineDownloader>> offlineDownloaders;


static void offlineDLStep()
{
  Platform& platform = *MapsApp::platform;
  while(!offlinePending.empty()) {
    if(offlineDownloaders.empty()) {
      auto& ol = offlinePending.front();  // safe because only this thread can remove final item from offlinePending
      for(auto& source : ol.sources)
        offlineDownloaders.emplace_back(new OfflineDownloader(platform, ol, source));
    }
    while(!offlineDownloaders.empty()) {
      //int nreq = std::max(1, maxOfflineDownloads - int(platform.activeUrlRequests()));
      auto& dl = offlineDownloaders.back();
      while(dl->fetchNextTile(maxOfflineDownloads)) {}
      if(dl->remainingTiles())
        return;
      LOGD("completed offline tile downloads for layer %s", dl->name.c_str());
      offlineDownloaders.pop_back();
    }
    LOG("completed offline tile downloads for map %d", offlinePending.front().id);

    MapsApp::runOnMainThread([id=offlinePending.front().id, canceled=offlinePending.front().canceled](){
      mapsOfflineInst->downloadCompleted(id, canceled);
    });
    offlinePending.pop_front();
  }
  //platform.onUrlRequestsThreshold = nullptr;  // all done!
}

static void offlineDLMain()
{
  semOfflineWorker.wait();
  while(runOfflineWorker) {
    offlineDLStep();
    semOfflineWorker.wait();
  }
}

#include "hash-library/md5.h"

static void udf_md5(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  if(argc != 1) {
    sqlite3_result_error(context, "sqlite md5() - Invalid number of arguments (1 required).", -1);
    return;
  }
  MD5 md5;
  int len = sqlite3_value_bytes(argv[0]);
  std::string hash = md5(sqlite3_value_blob(argv[0]), len);
  sqlite3_result_text(context, hash.c_str(), -1, SQLITE_TRANSIENT);
}

OfflineDownloader::OfflineDownloader(Platform& _platform, const OfflineMapInfo& ofl, const OfflineSourceInfo& src)
{
  mbtiles = std::make_unique<Tangram::MBTilesDataSource>(_platform, src.name, src.cacheFile, "", true);
  name = src.name + "-" + std::to_string(ofl.id);
  offlineId = ofl.id;
  searchData = MapsSearch::parseSearchFields(src.searchData);
  offlineSize = mbtiles->getOfflineSize();
  srcMaxZoom = std::min(ofl.maxZoom, src.maxZoom);

  // SQL import?
  if(src.url.substr(0, 6) == "ATTACH") {
    SQLiteDB tileDB(mbtiles->dbHandle());
    if(sqlite3_create_function(tileDB.db, "md5", 1, SQLITE_UTF8, 0, udf_md5, 0, 0) != SQLITE_OK)
      LOGE("SQL error creading md5() function: %s", tileDB.errMsg());
    else if(!tileDB.exec(src.url))
      LOGE("SQL error importing mbtiles: %s", tileDB.errMsg());
    else if(!searchData.empty()) {
      const char* newtilesSql = "SELECT tile_column, tile_row FROM map JOIN offline_tiles"
          " ON map.tile_id = offline_tiles.tile_id WHERE offline_id = ? AND zoom_level = ?";
      tileDB.stmt(newtilesSql).bind(offlineId, srcMaxZoom).exec([&](int x, int y){
        m_queued.emplace_back(x, (1 << srcMaxZoom) - 1 - y, srcMaxZoom);
      });
    }
    tileDB.release();
  }
  else {
    mbtiles->next = std::make_unique<Tangram::NetworkDataSource>(_platform, src.url, src.urlOptions);
    // if zoomed past srcMaxZoom, download tiles at srcMaxZoom
    for(int z = std::min(ofl.zoom, srcMaxZoom); z <= srcMaxZoom; ++z) {
      TileID tile00 = lngLatTile(ofl.lngLat00, z);
      TileID tile11 = lngLatTile(ofl.lngLat11, z);
      for(int x = tile00.x; x <= tile11.x; ++x) {
        for(int y = tile11.y; y <= tile00.y; ++y)  // note y tile index incr for decr latitude
          m_queued.emplace_back(x, y, z);
      }
    }
    // queue all z3 tiles so user sees world map when zooming out
    if(ofl.zoom > 3) {  // && cfg->Bool("offlineWorldMap")
      for(int x = 0; x < 8; ++x) {
        for(int y = 0; y < 8; ++y)
          m_queued.emplace_back(x, y, 3);
      }
    }
  }
  totalTiles = m_queued.size();
}

OfflineDownloader::~OfflineDownloader()
{
  MapsApp::platform->notifyStorage(0, mbtiles->getOfflineSize() - offlineSize);
}

void OfflineDownloader::cancel()
{
  std::unique_lock<std::mutex> lock(m_mutexQueue);
  m_queued.clear();
  canceled = true;  //m_pending.clear();
}

bool OfflineDownloader::fetchNextTile(int maxPending)
{
  std::unique_lock<std::mutex> lock(m_mutexQueue);
  if(m_queued.empty() || int(m_pending.size()) >= maxPending) return false;
  auto task = std::make_shared<BinaryTileTask>(m_queued.front(), nullptr);
  // prevent redundant write to offline_tiles table if importing from mbtiles file
  bool needdata = !searchData.empty() && task->tileId().z == srcMaxZoom;
  task->offlineId = mbtiles->next ? (needdata ? -offlineId : offlineId) : 0;
  m_pending.push_back(m_queued.front());
  m_queued.pop_front();
  lock.unlock();
  TileTaskCb cb{[this](std::shared_ptr<TileTask> _task) { tileTaskCallback(_task); }};
  mbtiles->loadTileData(task, cb);
  LOGD("%s: requested download of offline tile %s", name.c_str(), task->tileId().toString().c_str());
  return true;
}

void OfflineDownloader::tileTaskCallback(std::shared_ptr<TileTask> task)
{
  std::unique_lock<std::mutex> lock(m_mutexQueue);
  TileID tileId = task->tileId();
  auto pendingit = m_pending.begin();  //std::find(m_pending.begin(), m_pending.begin(), tileId);
  for(; pendingit != m_pending.end(); ++pendingit) {
    if(pendingit->x == tileId.x && pendingit->y == tileId.y && pendingit->z == tileId.z)
      break;
  }
  if(pendingit == m_pending.end()) {
    LOGW("Pending tile entry not found for tile!");
    return;
  }
  if(!canceled && !task->hasData()) {
    // put back in queue (at end) on failure
    if(++pendingit->s - pendingit->z <= maxRetries)  // use TileID.s to track number of retries
      m_queued.push_back(*pendingit);
    else
      LOGW("%s: download of offline tile %s failed", name.c_str(), task->tileId().toString().c_str());
  }
  m_pending.erase(pendingit);
  lock.unlock();

  if(!canceled && task->hasData()) {
    if(!searchData.empty() && task->tileId().z == srcMaxZoom)
      MapsSearch::indexTileData(task.get(), offlineId, searchData);
    LOGD("%s: completed download of offline tile %s", name.c_str(), task->tileId().toString().c_str());
  }
  Timestamp t0 = mSecSinceEpoch();
  if(t0 - prevProgressUpdate > 1000) {
    prevProgressUpdate = t0;
    MapsApp::runOnMainThread([](){ mapsOfflineInst->updateProgress(); });
  }
  semOfflineWorker.post();
}

void MapsOffline::saveOfflineMap(int mapid, LngLat lngLat00, LngLat lngLat11, int maxZoom)
{
  Map* map = app->map;
  // don't load tiles outside visible region at any zoom level (as using TileID::getChild() recursively
  //  would do - these could potentially outnumber the number of desired tiles!)
  double heightkm = lngLatDist(lngLat00, LngLat(lngLat00.longitude, lngLat11.latitude));
  double widthkm = lngLatDist(lngLat11, LngLat(lngLat00.longitude, lngLat11.latitude));
  int zoom = std::round(MapProjection::zoomAtMetersPerPixel( std::min(heightkm, widthkm)/MapProjection::tileSize() ));
  //int zoom = int(map->getZoom());
  // queue offline downloads
  offlinePending.push_back({mapid, lngLat00, lngLat11, zoom, maxZoom, {}, false});
  auto& tileSources = map->getScene()->tileSources();
  for(auto& src : tileSources) {
    auto& info = src->offlineInfo();
    if(info.cacheFile.empty())
      LOGE("Cannot save offline tiles for source %s - no cache file specified", src->name().c_str());
    else {
      offlinePending.back().sources.push_back(
          {src->name(), info.cacheFile, info.url, info.urlOptions, src->maxZoom(), {}});
      if(!src->isRaster())
        Tangram::YamlPath("global.search_data").get(map->getScene()->config(), offlinePending.back().sources.back().searchData);
    }
  }

  //MapsApp::platform->onUrlRequestsThreshold = [&](){ semOfflineWorker.post(); };  //onUrlClientIdle;
  //MapsApp::platform->urlRequestsThreshold = maxOfflineDownloads - 1;
  semOfflineWorker.post();
  runOfflineWorker = true;
  if(!offlineWorker)
    offlineWorker = std::make_unique<std::thread>(offlineDLMain);
}

MapsOffline::~MapsOffline()
{
  if(offlineWorker) {
    runOfflineWorker = false;
    semOfflineWorker.post();
    offlineWorker->join();
  }
}

int MapsOffline::numOfflinePending() const
{
  return offlinePending.queue.size();
}

bool MapsOffline::cancelDownload(int mapid)
{
  if(offlinePending.front().id == mapid) {
    offlinePending.front().canceled = true;
    std::unique_lock<std::mutex> lock(offlineDownloaders.mutex);
    for(auto& dl : offlineDownloaders.queue)
      dl->cancel();
    return false;
  }
  std::unique_lock<std::mutex> lock(offlinePending.mutex);
  offlinePending.queue.remove_if([mapid](auto a){ return a.id == mapid; });
  return true;
}

static void deleteOfflineMap(int mapid)
{
  int64_t offlineSize = 0;
  bool purge = MapsApp::config["storage"]["purge_offline"].as<bool>(true);
  FSPath cachedir(MapsApp::baseDir, "cache");
  for(auto& file : lsDirectory(cachedir)) {
    //for(auto& src : mapSources) ... this doesn't work because cache file may be specified in scene yaml
    //std::string cachename = src.second["cache"] ? src.second["cache"].Scalar() : src.first.Scalar();
    //std::string cachefile = app->baseDir + "cache/" + cachename + ".mbtiles";
    //if(cachename == "false" || !FSPath(cachefile).exists()) continue;
    FSPath cachefile = cachedir.child(file);
    if(cachefile.extension() != "mbtiles") continue;
    auto s = std::make_unique<Tangram::MBTilesDataSource>(
        *MapsApp::platform, cachefile.baseName(), cachefile.path, "", true);
    offlineSize -= s->getOfflineSize();
    s->deleteOfflineMap(mapid, purge);
    offlineSize += s->getOfflineSize();
  }
  MapsApp::platform->notifyStorage(0, offlineSize);  // this can trigger cache shrink, so wait until all sources processed
  MapsSearch::onDelOfflineMap(mapid);

  SQLiteStmt(MapsApp::bkmkDB, "DELETE FROM offlinemaps WHERE mapid = ?;").bind(mapid).exec();
}

void MapsOffline::downloadCompleted(int id, bool canceled)
{
  if(canceled)
    deleteOfflineMap(id);
  else
    SQLiteStmt(MapsApp::bkmkDB, "UPDATE offlinemaps SET done = 1 WHERE mapid = ?;").bind(id).exec();
  populateOffline();
}

void MapsOffline::resumeDownloads()
{
  // caller should restore map source if necessary
  const char* query = "SELECT mapid, lng0,lat0,lng1,lat1, maxzoom, source FROM offlinemaps WHERE done = 0 ORDER BY timestamp;";
  SQLiteStmt(app->bkmkDB, query).exec([&](int mapid, double lng0, double lat0, double lng1, double lat1, int maxZoom, std::string sourcestr) {
    app->mapsSources->rebuildSource(sourcestr, false);
    saveOfflineMap(mapid, LngLat(lng0, lat0), LngLat(lng1, lat1), maxZoom);
    LOG("Resuming offline map download for source %s", sourcestr.c_str());
  });
}

bool MapsOffline::importFile(std::string destsrc, std::string srcpath)
{
  // loading source will ensure mbtiles cache is created if enabled for source
  if(destsrc != app->mapsSources->currSource)
    app->mapsSources->rebuildSource(destsrc, false);

  SQLiteDB srcDB;
  if(sqlite3_open_v2(srcpath.c_str(), &srcDB.db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
    MapsApp::messageBox("Import map", fstring("Cannot import from %s: cannot open file", srcpath.c_str()), {"OK"});
    return false;
  }
  std::string srcFmt;
  srcDB.stmt("SELECT value FROM metadata WHERE name = 'format';").exec([&](const char* fmt){ srcFmt = fmt; });

  // check vector vs raster; mbtiles cache does not set json metadata field, so we cannot compare that
  Tangram::TileSource* tileSource = NULL;
  for(auto& tilesrc : app->map->getScene()->tileSources()) {
    if(!tilesrc->isRaster() == (srcFmt == "pbf")) {
      tileSource = tilesrc.get();
      break;
    }
  }
  std::string destpath = tileSource ? tileSource->offlineInfo().cacheFile : "";
  if(destpath.empty())
    destpath = tileSource->offlineInfo().url;
  if(destpath.empty() || Url::getPathExtension(destpath) != "mbtiles") {
    MapsApp::messageBox("Import map", "Cannot import to selected source: no cache file found", {"OK"});
    return false;
  }

  std::string importSql;
  int offlineId = int(time(NULL));
  bool hasTiles = false, hasMap = false, hasImages = false;
  srcDB.stmt("SELECT name FROM sqlite_master WHERE type = 'table';").exec([&](std::string tblname){
    if(tblname == "map") hasMap = true;
    else if(tblname == "images") hasImages = true;
    else if(tblname == "tiles") hasTiles = true;
  });
  if(hasTiles) {
    // CREATE TRIGGER IF NOT EXISTS insert_tile INSTEAD OF INSERT ON tiles
    // BEGIN
    //   INSERT INTO map VALUES(NEW.zoom_level, NEW.tile_column, NEW.tile_row, md5(NEW.tile_data));
    //   INSERT INTO images VALUES(NEW.tile_data, SELECT tile_id FROM map WHERE
    //     map.zoom_level = NEW.zoom_level AND map.tile_column = NEW.tile_column AND map.tile_row = NEW.tile_row);
    //   -- rowid == last_insert_rowid();
    // END;
    // since tiles from multiple sources can be merged into dest, we could have mixture of compressed and
    //  uncompressed (but should mostly be gzip), so set compression=unknown
    const char* query = R"#(ATTACH DATABASE '%s' AS src;
      BEGIN;
      REPLACE INTO map SELECT zoom_level, tile_column, tile_row, md5(tile_data) FROM src.tiles;
      DELETE FROM images WHERE tile_id NOT IN (SELECT tile_id FROM map);  -- delete orphaned tiles
      REPLACE INTO images SELECT s.tile_data, map.tile_id FROM src.tiles AS s JOIN map ON
        s.zoom_level = map.zoom_level AND s.tile_column = map.tile_column AND s.tile_row = map.tile_row;
      REPLACE INTO offline_tiles SELECT map.tile_id, %d FROM src.tiles AS s JOIN map ON
        s.zoom_level = map.zoom_level AND s.tile_column = map.tile_column AND s.tile_row = map.tile_row;
      REPLACE INTO metadata(name, value) VALUES ('compression', 'unknown');
      COMMIT;
      DETACH DATABASE src;)#";
    importSql = fstring(query, srcpath.c_str(), offlineId);
  }
  else if(hasMap && hasImages) {
    const char* query = R"#(ATTACH DATABASE '%s' AS src;
      BEGIN;
      REPLACE INTO map SELECT * FROM src.map;
      DELETE FROM images WHERE tile_id NOT IN (SELECT tile_id FROM map);  -- delete orphaned tiles
      REPLACE INTO images SELECT * FROM src.images;
      REPLACE INTO offline_tiles SELECT tile_id, %d FROM src.map;
      REPLACE INTO metadata(name, value) VALUES ('compression', 'unknown');
      COMMIT;
      DETACH DATABASE src;)#";
    importSql = fstring(query, srcpath.c_str(), offlineId);
  }
  else {
    MapsApp::messageBox("Import map", fstring("Import failed: unknown MBTiles schema in %s", srcpath.c_str()), {"OK"});
    return false;
  }

  LngLat lngLat00, lngLat11;
  int maxZoom = 0;
  const char* boundsSql = "SELECT min(tile_row), max(tile_row), min(tile_column), max(tile_column),"
      " max(zoom_level) FROM tiles WHERE zoom_level = (SELECT max(zoom_level) FROM tiles);";
  srcDB.stmt(boundsSql).exec([&](int min_row, int max_row, int min_col, int max_col, int max_zoom){
    maxZoom = max_zoom;
    lngLat00 = MapProjection::projectedMetersToLngLat(
        MapProjection::tileSouthWestCorner(TileID(min_col, (1 << max_zoom) - 1 - min_row, max_zoom)));
    lngLat11 = MapProjection::projectedMetersToLngLat(
        MapProjection::tileSouthWestCorner(TileID(max_col+1, (1 << max_zoom) - 1 - max_row - 1, max_zoom)));
  });
  sqlite3_close(srcDB.release());  // probably not necessary

  // note that we set done = 1 since import is not resumable
  std::string maptitle = FSPath(srcpath).baseName();
  const char* query = "INSERT INTO offlinemaps (mapid,lng0,lat0,lng1,lat1,maxzoom,source,title,done) VALUES (?,?,?,?,?,?,?,?,?);";
  SQLiteStmt(app->bkmkDB, query).bind(offlineId, lngLat00.longitude, lngLat00.latitude,
      lngLat11.longitude, lngLat11.latitude, maxZoom, app->mapsSources->currSource, maptitle, 1).exec();
  app->map->setCameraPositionEased(app->map->getEnclosingCameraPosition(lngLat00, lngLat11, {32}), 0.5f);

  offlinePending.push_back({offlineId, lngLat00, lngLat11, 0, maxZoom, {}, false});
  offlinePending.back().sources.push_back(
      {tileSource->name(), destpath, importSql, {}, tileSource->maxZoom(), {}});
  if(!tileSource->isRaster())
    Tangram::YamlPath("global.search_data").get(
        app->map->getScene()->config(), offlinePending.back().sources.back().searchData);

  semOfflineWorker.post();
  runOfflineWorker = true;
  if(!offlineWorker)
    offlineWorker = std::make_unique<std::thread>(offlineDLMain);

  return true;
}

// GUI

void MapsOffline::updateProgress()
{
  if(!offlinePanel->isVisible()) return;
  std::unique_lock<std::mutex> lock(offlinePending.mutex);

  for(Widget* item : offlineContent->select(".listitem")) {
    int mapid = item->node->getIntAttr("__mapid");
    int ii = 0;
    for(auto& ol : offlinePending.queue) {
      if(ol.id == mapid) {
        if(ol.canceled)
          item->selectFirst(".detail-text")->setText("Canceling...");
        else if(ii == 0) {
          std::unique_lock<std::mutex> lock2(offlineDownloaders.mutex);
          int total = 0, remaining = 0;
          for(auto& dl : offlineDownloaders.queue) {
            total += dl->totalTiles;
            remaining += dl->remainingTiles();
          }
          item->selectFirst(".detail-text")->setText(fstring("%d/%d tiles downloaded", total - remaining, total).c_str());
        }
        else
          item->selectFirst(".detail-text")->setText("Download pending");
        //item->selectFirst(".delete-btn")->setText("Cancel");
        break;
      }
      ++ii;
    }
  }
}

void MapsOffline::populateOffline()
{
  app->gui->deleteContents(offlineContent, ".listitem");

  const char* query = "SELECT mapid, lng0,lat0,lng1,lat1, source, title, timestamp FROM offlinemaps ORDER BY timestamp DESC;";
  SQLiteStmt(app->bkmkDB, query).exec([&](int mapid, double lng0, double lat0, double lng1, double lat1,
      std::string sourcestr, std::string titlestr, int64_t timestamp){
    auto srcinfo = app->mapsSources->mapSources[sourcestr];

    std::string detail = srcinfo ? srcinfo["title"].Scalar() : sourcestr;
    detail.append(u8" \u2022 ").append(ftimestr("%F %H:%M:%S", timestamp*1000));
    Button* item = createListItem(MapsApp::uiIcon("fold-map"), titlestr.c_str(), detail.c_str());
    Widget* container = item->selectFirst(".child-container");
    item->node->setAttr("__mapid", mapid);
    item->onClicked = [=](){
      bool checked = !item->isChecked();
      for(Widget* w : offlineContent->select(".listitem"))
        static_cast<Button*>(w)->setChecked(checked && static_cast<Button*>(w) == item);
      if(!checked) {
        app->map->markerSetVisible(rectMarker, false);
        return;
      }
      // show bounds of offline region on map
      LngLat bounds[5] = {{lng0, lat0}, {lng0, lat1}, {lng1, lat1}, {lng1, lat0}, {lng0, lat0}};
      if(!rectMarker)
        rectMarker = app->map->markerAdd();
      else
        app->map->markerSetVisible(rectMarker, true);
      app->map->markerSetStylingFromString(rectMarker, polylineStyle);
      app->map->markerSetPolyline(rectMarker, bounds, 5);
      app->map->setCameraPositionEased(app->map->getEnclosingCameraPosition(bounds[0], bounds[2], {32}), 0.5f);
      if(app->mapsSources->currSource != sourcestr)
        app->mapsSources->rebuildSource(sourcestr);
    };

    Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowMenu->addItem("Delete", [=](){
      if(rectMarker)
        app->map->markerSetVisible(rectMarker, false);
      if(cancelDownload(mapid)) {
        deleteOfflineMap(mapid);
        populateOffline();
      }
      else
        updateProgress();
    });
    overflowBtn->setMenu(overflowMenu);
    container->addWidget(overflowBtn);

    offlineContent->addWidget(item);
  });
  updateProgress();
}

Widget* MapsOffline::createPanel()
{
  mapsOfflineInst = this;
  // should we include zoom? total bytes?
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS offlinemaps(mapid INTEGER PRIMARY KEY,"
      " lng0 REAL, lat0 REAL, lng1 REAL, lat1 REAL, maxzoom INTEGER, source TEXT, title TEXT,"
      " done INTEGER DEFAULT 0, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));");

  TextEdit* titleEdit = createTitledTextEdit("Title");
  SpinBox* maxZoomSpin = createSpinBox(13, 1, 1, 20, "%.0f");
  Widget* maxZoomRow = createTitledRow("Max zoom", maxZoomSpin);

  auto downloadFn = [=](){
    LngLat lngLat00, lngLat11;
    app->getMapBounds(lngLat00, lngLat11);
    int offlineId = int(time(NULL));
    int maxZoom = int(maxZoomSpin->value());
    saveOfflineMap(offlineId, lngLat00, lngLat11, maxZoom);
    const char* query = "INSERT INTO offlinemaps (mapid,lng0,lat0,lng1,lat1,maxzoom,source,title) VALUES (?,?,?,?,?,?,?,?);";
    SQLiteStmt(app->bkmkDB, query).bind(offlineId, lngLat00.longitude, lngLat00.latitude,
        lngLat11.longitude, lngLat11.latitude, maxZoom, app->mapsSources->currSource, titleEdit->text()).exec();
    populateOffline();
    auto item = static_cast<Button*>(offlineContent->selectFirst(".listitem"));
    if(item) item->onClicked();
  };

  Widget* downloadPanel = createInlineDialog({titleEdit, maxZoomRow}, "Download", downloadFn);  //createColumn();

  Button* openBtn = createToolbutton(MapsApp::uiIcon("open-folder"), "Install Offline Map");

  auto openMapFn = [this](const char* outPath){
    std::string srcpath(outPath);
    std::vector<std::string> layerKeys;
    std::vector<std::string> layerTitles;
    for(const auto& src : app->mapsSources->mapSources) {
      std::string type = src.second["type"].Scalar();
      if(type == "Raster" || type == "Vector") {
        layerKeys.push_back(src.first.Scalar());
        layerTitles.push_back(src.second["title"].Scalar());
      }
    }
    if(!selectDestDialog)
      selectDestDialog.reset(createSelectDialog("Choose source", MapsApp::uiIcon("layers")));
    selectDestDialog->addItems(layerTitles);
    selectDestDialog->onSelected = [=](int idx){
      if(importFile(layerKeys[idx], srcpath))
        populateOffline();
    };
    showModalCentered(selectDestDialog.get(), MapsApp::gui);
  };

  openBtn->onClicked = [=](){ MapsApp::openFileDialog({{"MBTiles files", "mbtiles"}}, openMapFn); };

  Button* saveBtn = createToolbutton(MapsApp::uiIcon("download"), "Save Offline Map");
  saveBtn->onClicked = [=](){
    titleEdit->setText(ftimestr("%FT%H.%M.%S").c_str());
    titleEdit->selectAll();

    int maxZoom = 0;
    auto& tileSources = app->map->getScene()->tileSources();
    for(auto& src : tileSources)
      maxZoom = std::max(maxZoom, src->maxZoom());
    maxZoomSpin->setLimits(1, maxZoom);
    if(maxZoomSpin->value() > maxZoom)
      maxZoomSpin->setValue(maxZoom);

    showInlineDialogModal(downloadPanel);
  };

  offlineContent = createColumn();
  auto toolbar = app->createPanelHeader(MapsApp::uiIcon("offline"), "Offline Maps");
  toolbar->addWidget(openBtn);
  toolbar->addWidget(saveBtn);
  offlinePanel = app->createMapPanel(toolbar, offlineContent, NULL, false);

  offlinePanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == MapsApp::PANEL_CLOSED) {
      if(rectMarker)
        app->map->markerSetVisible(rectMarker, false);
    }
    return false;
  });

  offlineContent->addWidget(downloadPanel);

  Button* offlineBtn = createToolbutton(MapsApp::uiIcon("offline"), "Offline Maps");
  offlineBtn->onClicked = [this](){
    app->showPanel(offlinePanel, true);
    populateOffline();
  };
  return offlineBtn;
}
