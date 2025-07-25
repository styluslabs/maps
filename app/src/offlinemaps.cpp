#include "offlinemaps.h"
#include "mapsapp.h"
#include "mapsearch.h"
#include "mapsources.h"
#include "util.h"
#include <deque>
// "private" headers
#include "scene/scene.h"
#include "data/mbtilesDataSource.h"
#include "data/networkDataSource.h"

#include "usvg/svgpainter.h"
#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "mapwidgets.h"

static bool runOfflineWorker = false;
static std::unique_ptr<std::thread> offlineWorker;
static Semaphore semOfflineWorker(1);


// Offline maps
// - initial discussion https://github.com/tangrams/tangram-es/issues/931

struct OfflineSourceInfo
{
  std::string name;
  TileSource::OfflineInfo info;
  int maxZoom;
  YAML::Node searchData;
};

struct OfflineMapInfo
{
  OfflineMapInfo(int _id, LngLat ll00, LngLat ll11, int _zoom, int _maxzoom)
    : id(_id), lngLat00(ll00), lngLat11(ll11), zoom(_zoom), maxZoom(_maxzoom) {}
  //OfflineMapInfo(OfflineMapInfo&&) = default;
  //OfflineMapInfo(const OfflineMapInfo&) { assert(false); }

  int id;
  LngLat lngLat00, lngLat11;
  int zoom, maxZoom;
  std::vector<OfflineSourceInfo> sources;
  YAML::Node globals;
  std::unique_ptr<Tangram::DataSourceContext> srcContext;
};

struct OfflineTask
{
  OfflineTask(int _id, std::function<void()>&& _fn) : id(_id), fn(std::move(_fn)) {}
  int id;
  bool canceled = false;
  std::function<void()> fn;
};

class OfflineDownloader
{
public:
    OfflineDownloader(const OfflineMapInfo& ofl, const OfflineSourceInfo& src);
    //~OfflineDownloader();
    size_t remainingTiles() const { return m_queued.size() + m_pending.size(); }
    bool fetchNextTile(int maxPending);
    void cancel();
    int64_t getOfflineSize();
    std::string name;
    int maxRetries = 4;

private:
    void tileTaskCallback(std::shared_ptr<TileTask> _task);

    int offlineId;
    int srcMaxZoom;
    bool canceled = false;
    std::deque<TileID> m_queued;
    std::vector<TileID> m_pending;
    std::mutex m_mutexQueue;
    std::shared_ptr<TileSource> tileSource;
    std::shared_ptr<Tangram::ScenePrana> scenePrana;
    Tangram::MBTilesDataSource* mbTiles;
    std::vector<SearchData> searchData;
};

static MapsOffline* mapsOfflineInst = NULL;  // for updateProgress()
static int maxOfflineDownloads = 8;
static int currTilesTotal = 0;
static int64_t currTilesSize = 0;
static std::atomic<Timestamp> prevProgressUpdate(0);
static ThreadSafeQueue<OfflineTask, std::list> offlinePending;
static ThreadSafeQueue<std::unique_ptr<OfflineDownloader>> offlineDownloaders;


static void offlineDLStep()
{
  while(!offlinePending.empty()) {
    auto& olinfo = offlinePending.front();  // safe because only this thread can remove final item from offlinePending
    if(offlineDownloaders.empty()) {
      olinfo.fn();
      if(!offlineDownloaders.empty()) {
        for(auto& dl : offlineDownloaders.queue)
          currTilesTotal += dl->remainingTiles();
      }
    }
    while(!offlineDownloaders.empty()) {
      //int nreq = std::max(1, maxOfflineDownloads - int(platform.activeUrlRequests()));
      auto& currdl = offlineDownloaders.back();
      while(currdl->fetchNextTile(maxOfflineDownloads)) {}
      // update GUI
      Timestamp t0 = mSecSinceEpoch();
      if(t0 - prevProgressUpdate > 1000) {
        int remaining = 0;
        for(auto& dl : offlineDownloaders.queue)
          remaining += dl->remainingTiles();
        prevProgressUpdate = t0;
        MapsApp::runOnMainThread([=, id=olinfo.id](){
          auto msg = fstring("%d/%d tiles downloaded", currTilesTotal - remaining, currTilesTotal);
          mapsOfflineInst->updateProgress(id, msg);
        });
      }
      if(currdl->remainingTiles())
        return;
      int64_t olsize = currdl->getOfflineSize();
      currTilesSize += olsize;
      LOGD("completed offline tile downloads for layer %s", currdl->name.c_str());
      offlineDownloaders.pop_back();
    }
    LOG("completed offline tile downloads for map %d", olinfo.id);

    MapsApp::runOnMainThread([id=olinfo.id, canceled=olinfo.canceled, s=currTilesSize](){
      mapsOfflineInst->downloadCompleted(id, canceled, s);
    });
    offlinePending.pop_front();
    currTilesTotal = 0;
    currTilesSize = 0;
  }
}

static void offlineDLMain()
{
  semOfflineWorker.wait();
  while(runOfflineWorker) {
    offlineDLStep();
    semOfflineWorker.wait();
  }
}

OfflineDownloader::OfflineDownloader(const OfflineMapInfo& ofl, const OfflineSourceInfo& src)
{
  auto mbtiles = std::make_unique<Tangram::MBTilesDataSource>(
        ofl.srcContext->getPlatform(), src.name, src.info.cacheFile, "", true);
  mbTiles = mbtiles.get();
  name = src.name + "-" + std::to_string(ofl.id);
  offlineId = ofl.id;
  searchData = MapsSearch::parseSearchFields(src.searchData);
  srcMaxZoom = std::min(ofl.maxZoom, src.maxZoom);

  mbtiles->next = std::make_unique<Tangram::NetworkDataSource>(*ofl.srcContext, src.info.url, src.info.urlOptions);
  // TileSource shared_ptr is needed for thread synchronization in DataSources
  tileSource = std::make_shared<TileSource>(name, std::move(mbtiles), TileSource::ZoomOptions());
  tileSource->setFormat(src.info.format);
  scenePrana = std::make_shared<Tangram::ScenePrana>(nullptr);
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

int64_t OfflineDownloader::getOfflineSize()
{
  // get size of tiles belonging only to this map
  int64_t dsize = 0;
  mbTiles->getDB()->stmt(
      "SELECT sum(length(tile_data)) FROM images WHERE tile_id IN (SELECT tile_id FROM offline_tiles WHERE"
      " offline_id = ?1 AND tile_id NOT IN (SELECT tile_id FROM offline_tiles WHERE offline_id <> ?1));")
    .bind(offlineId).onerow(dsize);
  return dsize;
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
  auto task = std::make_shared<BinaryTileTask>(m_queued.front(), tileSource.get());
  task->setScenePrana(scenePrana);
  // prevent redundant write to offline_tiles table if importing from mbtiles file
  bool needdata = !searchData.empty() && task->tileId().z == srcMaxZoom;
  task->offlineId = mbTiles->next ? (needdata ? -offlineId : offlineId) : 0;
  m_pending.push_back(m_queued.front());
  m_queued.pop_front();
  lock.unlock();
  TileTaskCb cb{[this](std::shared_ptr<TileTask> _task) { tileTaskCallback(_task); }};
  mbTiles->loadTileData(task, cb);
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
      LOGW("%s: download of offline tile %s failed", name.c_str(), tileId.toString().c_str());
  }
  m_pending.erase(pendingit);
  lock.unlock();

  if(!canceled && task->hasData()) {
    if(!searchData.empty() && tileId.z == srcMaxZoom)
      MapsSearch::indexTileData(task.get(), offlineId, searchData);
    LOGD("%s: completed download of offline tile %s", name.c_str(), tileId.toString().c_str());
  }
  semOfflineWorker.post();
}

void MapsOffline::runSQL(std::string dbpath, std::string sql)
{
  SQLiteDB db;
  if(db.open(dbpath, SQLITE_OPEN_READWRITE) != SQLITE_OK)
    LOGE("Cannot open %s", dbpath.c_str());
  else if(!db.exec(sql))
    LOGE("SQL error: %s", db.errMsg());
}

int64_t MapsOffline::shrinkCache(int64_t maxbytes)
{
  std::vector<SQLiteDB> dbsources;
  std::vector< std::pair<int, int> > tiles;
  int totalTiles = 0;
  auto insertTile = [&](const char*, int oflid, int t, int size){
    ++totalTiles;
    if(!oflid) { tiles.emplace_back(t, size); }
  };

  FSPath cachedir(MapsApp::baseDir, "cache");
  for(auto& file : lsDirectory(cachedir)) {
    FSPath cachefile = cachedir.child(file);
    if(cachefile.extension() != "mbtiles") continue;
    SQLiteDB mbtiles;
    if(mbtiles.open(cachefile.path, SQLITE_OPEN_READWRITE) != SQLITE_OK) { continue; }
    int ntiles = totalTiles;
    bool ok = mbtiles.stmt("SELECT images.tile_id, ot.offline_id, tla.last_access, length(tile_data)"
        " FROM images LEFT JOIN tile_last_access AS tla ON images.tile_id = tla.tile_id"
        " LEFT JOIN offline_tiles AS ot ON images.tile_id = ot.tile_id;").exec(insertTile);
    if(!ok) {
      LOGW("Error getting tile sizes from %s - database may be in use.", cachefile.c_str());
      continue;
    }
    dbsources.push_back(std::move(mbtiles));
    // delete empty cache file
    if(totalTiles == ntiles) {
      LOG("Deleting empty cache file %s", cachefile.c_str());
      dbsources.pop_back();  // we push then pop because DB has to be closed before we can remove file
      removeFile(cachefile.path);
    }
    else
      LOG("Found %d tiles in %s", totalTiles - ntiles, cachefile.c_str());
  }

  std::sort(tiles.rbegin(), tiles.rend());  // sort by timestamp, descending (newest to oldest)
  int64_t tot = 0;
  for(auto& x : tiles) {
    tot += x.second;
    if(tot > maxbytes) {
      for(auto& src : dbsources) {
        int totchanges = src.totalChanges();
        src.stmt("DELETE FROM images WHERE tile_id NOT IN (SELECT tile_id FROM tile_last_access WHERE"
            " last_access > ?) AND tile_id NOT IN (SELECT tile_id FROM offline_tiles);").bind(x.first).exec();
        int nchanges = src.totalChanges() - totchanges;
        LOG("shrinkCache: %d changes for %s", nchanges, sqlite3_db_filename(src.db, "main"));
        if(nchanges > 32)
          src.exec("VACUUM;");
      }
      break;
    }
  }
  return tot;
}

void MapsOffline::queueOfflineTask(int mapid, std::function<void()>&& fn)
{
  offlinePending.emplace_back(mapid, std::move(fn));
  semOfflineWorker.post();
  runOfflineWorker = true;
  if(!offlineWorker)
    offlineWorker = std::make_unique<std::thread>(offlineDLMain);
}

void MapsOffline::saveOfflineMap(int mapid, LngLat lngLat00, LngLat lngLat11, int maxZoom)
{
  Map* map = app->map.get();
  // don't load tiles outside visible region at any zoom level (as using TileID::getChild() recursively
  //  would do - these could potentially outnumber the number of desired tiles!)
  double heightkm = lngLatDist(lngLat00, LngLat(lngLat00.longitude, lngLat11.latitude));
  double widthkm = lngLatDist(lngLat11, LngLat(lngLat00.longitude, lngLat11.latitude));
  int zoom = std::round(MapProjection::zoomAtMetersPerPixel(
      1000*std::min(heightkm, widthkm)/MapProjection::tileSize() ));
  //int zoom = int(map->getZoom());
  // queue offline downloads

  // shared_ptr needed due to std::function
  auto olinfo = std::make_shared<OfflineMapInfo>(mapid, lngLat00, lngLat11, zoom, maxZoom);
  auto& tileSources = map->getScene()->tileSources();
  for(auto& src : tileSources) {
    auto& info = src->offlineInfo();
    if(info.cacheFile.empty())
      LOGE("Cannot save offline tiles for source %s - no cache file specified", src->name().c_str());
    else {
      olinfo->sources.push_back({src->name(), info, src->maxZoom(), {}});
      // add header to indicate offline map download so server can deprioritize if needed
      olinfo->sources.back().info.urlOptions.httpOptions.addHeader("X-Tile-Priority", "background");
      if(!src->isRaster()) {
        // search data must remain accessible even if map source changed
        olinfo->sources.back().searchData = app->sceneConfig()["application"]["search_data"].clone();
      }
    }
  }

  olinfo->globals = app->sceneConfig()["globals"].clone();
  // shared_ptr needed due to std::function (note DataSourceContext itself is not copyable but must live as
  //  long as the lambda, i.e., can't be created in lambda call)
  olinfo->srcContext = std::make_unique<Tangram::DataSourceContext>(*MapsApp::platform, olinfo->globals);
  queueOfflineTask(mapid, [olinfo=std::move(olinfo)](){
    for(auto& source : olinfo->sources)
      offlineDownloaders.emplace_back(new OfflineDownloader(*olinfo, source));
  });
  //MapsApp::platform->onUrlRequestsThreshold = [&](){ semOfflineWorker.post(); };  //onUrlClientIdle;
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
  return offlinePending.size();
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

// run on offline worker thread - so we expect bkmkDB and searchDB to be in serialized mode
static void deleteOfflineMap(int mapid)
{
  int64_t dtotal = 0;
  int64_t storageShrinkMax = MapsApp::config["storage"]["shrink_at"].as<int64_t>(500) * 1024*1024;
  bool purge = MapsApp::config["storage"]["purge_offline"].as<bool>(true);
  FSPath cachedir(MapsApp::baseDir, "cache");
  for(auto& file : lsDirectory(cachedir)) {
    //for(auto& src : mapSources) ... this doesn't work because cache file may be specified in scene yaml
    FSPath cachefile = cachedir.child(file);
    if(cachefile.extension() != "mbtiles") continue;
    SQLiteDB mbtiles;
    if(mbtiles.open(cachefile.path, SQLITE_OPEN_READWRITE) != SQLITE_OK) { continue; }
    int64_t dsize = 0;
    mbtiles.stmt("SELECT sum(length(tile_data)) FROM images WHERE tile_id IN (SELECT tile_id FROM offline_tiles"
        " WHERE offline_id = ?1 AND tile_id NOT IN (SELECT tile_id FROM offline_tiles WHERE offline_id <> ?1));")
      .bind(mapid).onerow(dsize);
    if(dsize > 0 && purge) {
      // this can be quite slow
      mbtiles.stmt("DELETE FROM images WHERE tile_id IN (SELECT tile_id FROM offline_tiles WHERE"
          " offline_id = ?1 AND tile_id NOT IN (SELECT tile_id FROM offline_tiles WHERE offline_id <> ?1));")
        .bind(mapid).exec();
    }
    // if the same tiles were imported multiple times, we could have dsize == 0 even with offline map present
    mbtiles.stmt("DELETE FROM offline_tiles WHERE offline_id = ?;").bind(mapid).exec();
    if(purge && storageShrinkMax > 0 && dsize > 8*1024*1024)
      mbtiles.exec("VACUUM");  // also slow, obviously
    dtotal += dsize;
  }
  MapsSearch::onDelOfflineMap(mapid);
  SQLiteStmt(MapsApp::bkmkDB, "DELETE FROM offlinemaps WHERE mapid = ?;").bind(mapid).exec();
  MapsApp::platform->notifyStorage(0, -dtotal);  // this can trigger cache shrink, so wait until all sources processed
}

void MapsOffline::downloadCompleted(int id, bool canceled, int64_t size)
{
  if(!id) { return; }
  if(size <= 0) { size = 1; }  // for done flag in DB
  else { MapsApp::platform->notifyStorage(0, size); }
  if(canceled)
    MapsOffline::queueOfflineTask(-1, [=](){ deleteOfflineMap(id); });
  else if(id > 0)
    SQLiteStmt(MapsApp::bkmkDB, "UPDATE offlinemaps SET done = ? WHERE mapid = ?;").bind(size, id).exec();
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

void MapsOffline::openForImport(std::unique_ptr<PlatformFile> srcfile)
{
  SQLiteDB srcDB;
  if(srcDB.open(srcfile->sqliteURI(), SQLITE_OPEN_READONLY) != SQLITE_OK) {
    MapsApp::messageBox("Import error", fstring("Cannot import from %s: cannot open file", srcfile->fsPath().c_str()), {"OK"});
    return;
  }
  std::string srcFmt, desc, pois;
  srcDB.stmt("SELECT value FROM metadata WHERE name = 'format';").onerow(srcFmt);
  srcDB.stmt("SELECT value FROM metadata WHERE name = 'description';").onerow(desc);
  bool hasPois = srcDB.stmt("SELECT name FROM sqlite_master WHERE type='table' AND name='pois';").onerow(pois);

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
  sqlite3_close(srcDB.release());
  if(maxZoom <= 0) {
    MapsApp::messageBox("Import error", fstring("Cannot import from %s: no tiles found", srcfile->fsPath().c_str()), {"OK"});
    return;
  }

  int offlineId = int(time(NULL));
  OfflineMapInfo olinfo(offlineId, lngLat00, lngLat11, 0, maxZoom);

  if(app->mapsSources->mapSources[desc]) {
    if(importFile(desc, std::move(srcfile), std::move(olinfo), hasPois)) {
      populateOffline();
      updateProgress(offlineId, "Importing...");
    }
  }
  else {
    std::vector<std::string> layerKeys;
    std::vector<std::string> layerTitles;
    for(auto src : app->mapsSources->mapSources.const_pairs()) {
      if(srcFmt == "pbf" ? src.second["scene"] : src.second["url"]) {
        layerKeys.push_back(src.first.Scalar());
        layerTitles.push_back(src.second["title"].Scalar());
      }
    }
    if(!selectDestDialog)
      selectDestDialog.reset(createSelectDialog("Choose source", MapsApp::uiIcon("layers")));
    selectDestDialog->addItems(layerTitles);
    auto olinfop = std::make_shared<OfflineMapInfo>(std::move(olinfo));
    selectDestDialog->onSelected = [=, olinfop=std::move(olinfop), _srcfile=srcfile.release()](int idx) mutable {
      if(importFile(layerKeys[idx], std::unique_ptr<PlatformFile>(_srcfile), std::move(*olinfop), hasPois)) {
        populateOffline();
        updateProgress(offlineId, "Importing...");
      }
    };
    showModalCentered(selectDestDialog.get(), MapsApp::gui);
  }
}

#include "util/zlibHelper.h"
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

static bool mbtilesImport(SQLiteDB& tileDB, int offlineId)
{
  if(sqlite3_create_function(tileDB.db, "md5", 1, SQLITE_UTF8, 0, udf_md5, 0, 0) != SQLITE_OK) {
    LOGE("SQL error creating md5() function: %s", tileDB.errMsg());
    return false;
  }

  const char* importSql = NULL;
  bool hasTiles = false, hasMap = false, hasImages = false;
  tileDB.stmt("SELECT name FROM src.sqlite_master WHERE type = 'table';").exec([&](std::string tblname){
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
    importSql = R"#(BEGIN;
      REPLACE INTO map SELECT zoom_level, tile_column, tile_row, md5(tile_data) FROM src.tiles;
      DELETE FROM images WHERE tile_id NOT IN (SELECT tile_id FROM map);  -- delete orphaned tiles
      REPLACE INTO images (tile_data, tile_id) SELECT s.tile_data, map.tile_id FROM src.tiles AS s JOIN map ON
        s.zoom_level = map.zoom_level AND s.tile_column = map.tile_column AND s.tile_row = map.tile_row;
      REPLACE INTO offline_tiles (tile_id, offline_id) SELECT map.tile_id, %d FROM src.tiles AS s JOIN map ON
        s.zoom_level = map.zoom_level AND s.tile_column = map.tile_column AND s.tile_row = map.tile_row;
      REPLACE INTO metadata (name, value) VALUES ('compression', 'unknown');
      COMMIT;)#";
  }
  else if(hasMap && hasImages) {
    importSql = R"#(BEGIN;
      REPLACE INTO main.map SELECT * FROM src.map;
      DELETE FROM main.images WHERE tile_id NOT IN (SELECT tile_id FROM main.map);  -- delete orphaned tiles
      REPLACE INTO main.images (tile_data, tile_id) SELECT * FROM src.images;
      REPLACE INTO main.offline_tiles (tile_id, offline_id) SELECT tile_id, %d FROM src.map;
      REPLACE INTO main.metadata (name, value) VALUES ('compression', 'unknown');
      COMMIT;)#";
  }
  else {
    //MapsApp::messageBox("Import map", fstring("Import failed: unknown MBTiles schema in %s", srcpath.c_str()), {"OK"});
    LOGE("import source is not a valid mbtiles file");
    return false;
  }
  if(!tileDB.exec(fstring(importSql, offlineId).c_str())) {
    LOGE("SQL error on tile import: %s", tileDB.errMsg());
    return false;
  }
  return true;
}

static void indexImportedTiles(SQLiteDB& tileDB, int offlineId, const YAML::Node& searchYaml, int idxzoom)
{
  bool& canceled = offlinePending.front().canceled;
  if(canceled) return;
  auto searchData = MapsSearch::parseSearchFields(searchYaml);
  int total = 0, remaining = 0;
  const char* nSrcTilesSql = "SELECT count(1) FROM src.tiles WHERE zoom_level = ?";
  tileDB.stmt(nSrcTilesSql).bind(idxzoom).exec([&](int n){ remaining = total = n; });
  const char* newtilesSql = "SELECT tile_data, tile_column, tile_row FROM src.tiles WHERE zoom_level = ?";
  tileDB.stmt(newtilesSql).bind(idxzoom).exec([&](sqlite3_stmt* stmt){
    if(canceled) return;
    const char* blob = (const char*) sqlite3_column_blob(stmt, 0);
    const int length = sqlite3_column_bytes(stmt, 0);
    const int x = sqlite3_column_int(stmt, 1);
    const int y = sqlite3_column_int(stmt, 2);
    BinaryTileTask task(TileID(x, (1 << idxzoom) - 1 - y, idxzoom), nullptr);
    task.rawTileData = std::make_shared<std::vector<char>>();
    auto& _data = *task.rawTileData;
    if(Tangram::zlib_inflate(blob, length, _data) != 0) {
      _data.resize(length);
      memcpy(_data.data(), blob, length);
    }
    MapsSearch::indexTileData(&task, offlineId, searchData);
    --remaining;

    Timestamp t0 = mSecSinceEpoch();
    if(t0 - prevProgressUpdate > 1000) {
      prevProgressUpdate = t0;
      MapsApp::runOnMainThread([=](){
        auto msg = fstring("%d/%d tiles indexed", total - remaining, total);
        mapsOfflineInst->updateProgress(offlineId, msg);
      });
    }
  });
}

static void exportPOIs(const char* dest, int offlineId)
{
  static const char* poiExportSQL = R"#(ATTACH DATABASE 'file://%s?mode=ro' AS searchdb;
    BEGIN;
    DROP TABLE IF EXISTS main.pois;
    CREATE TABLE main.pois AS SELECT * FROM searchdb.pois WHERE tile_id IN
        (SELECT tile_id FROM searchdb.offline_tiles WHERE offline_id = %d);
    COMMIT;
    DETACH DATABASE searchdb;
  )#";
  FSPath searchDB(MapsApp::baseDir, "fts1.sqlite");
  SQLiteDB poiOutDB;
  if(poiOutDB.open(dest, SQLITE_OPEN_READWRITE) != SQLITE_OK) {
    LOGE("Error opening %s for POI export", dest);
    return;
  }
  if(poiOutDB.exec(fstring(poiExportSQL, searchDB.c_str(), offlineId))) {
    int nPois = 0;
    poiOutDB.stmt("SELECT count(1) FROM main.pois;").onerow(nPois);
    if(!nPois) {
      poiOutDB.exec("DROP TABLE IF EXISTS main.pois;");
      LOGW("No POIs found for export to %s", dest);
    }
    else
      LOG("Exported %d POIs to %s", nPois, dest);
  }
  else
    LOGE("SQL error exporting POIs to %s: %s", dest, poiOutDB.errMsg());
}

bool MapsOffline::importFile(std::string destsrc, std::unique_ptr<PlatformFile> srcfile, OfflineMapInfo olinfo, bool hasPois)
{
  bool poiimport = hasPois && app->config["storage"]["import_pois"].as<bool>(true);
  bool poiexport = !hasPois && app->config["storage"]["export_pois"].as<bool>(false);
  // loading source will ensure mbtiles cache is created if enabled for source
  if(MapsApp::terrain3D || destsrc != app->mapsSources->currSource) {
    bool was3d = std::exchange(MapsApp::terrain3D, false);
    app->mapsSources->rebuildSource(destsrc, false);
    MapsApp::terrain3D = was3d;
  }

  // check vector vs raster; mbtiles cache does not set json metadata field, so we cannot compare that
  auto& tilesrcs = app->map->getScene()->tileSources();
  if(tilesrcs.size() != 1) {
    MapsApp::messageBox("Import error", "Expected exactly one tile source for current source.", {"OK"});
    return false;
  }

  auto* tileSource = tilesrcs.front().get();
  std::string destpath = tileSource->offlineInfo().cacheFile;
  if(destpath.empty())
    destpath = tileSource->offlineInfo().url;
  if(destpath.empty() || Url::getPathExtension(destpath) != "mbtiles") {
    MapsApp::messageBox("Import error", "Cannot import to selected source: no cache file found", {"OK"});
    return false;
  }

  // note that we set done = 1 since import is not resumable
  std::string maptitle = FSPath(srcfile->fsPath()).baseName();
  const char* query = "INSERT INTO offlinemaps (mapid,lng0,lat0,lng1,lat1,maxzoom,source,title,done) VALUES (?,?,?,?,?,?,?,?,?);";
  SQLiteStmt(app->bkmkDB, query).bind(olinfo.id, olinfo.lngLat00.longitude, olinfo.lngLat00.latitude,
      olinfo.lngLat11.longitude, olinfo.lngLat11.latitude, olinfo.maxZoom, app->mapsSources->currSource, maptitle, 1).exec();

  app->lookAt(olinfo.lngLat00, olinfo.lngLat11);

  std::shared_ptr<YAML::Node> searchYaml;
  if(!poiimport && !tileSource->isRaster())
      searchYaml = std::make_shared<YAML::Node>(app->sceneConfig()["application"]["search_data"].clone());

  int offlineId = olinfo.id, srcMaxZoom = tileSource->maxZoom();
  queueOfflineTask(offlineId, [=, searchYaml=std::move(searchYaml), _srcfile=srcfile.release()](){
    std::unique_ptr<PlatformFile> srcfile(_srcfile);
    SQLiteDB tileDB;
    if(tileDB.open(destpath, SQLITE_OPEN_READWRITE) != SQLITE_OK) {
      LOGE("Error opening cache DB %s for import: %s", destpath.c_str(), tileDB.errMsg());
      return;
    }
    if(!tileDB.exec(fstring("ATTACH DATABASE '%s' AS src;", srcfile->sqliteURI().c_str()))) {
      LOGE("SQL error attaching mbtiles: %s", tileDB.errMsg());
      return;
    }
    if(!mbtilesImport(tileDB, offlineId))
      return;
    // refresh map to show new tiles
    app->mapsSources->rebuildSource(destsrc);
    if(searchYaml)  // !poiimport)
      indexImportedTiles(tileDB, offlineId, *searchYaml, srcMaxZoom);
    // detach src DB from tile DB before attaching to search index DB
    if(!tileDB.exec("DETACH DATABASE src;"))
      LOGE("SQL error detaching mbtiles: %s", tileDB.errMsg());
    if(poiimport)
      MapsSearch::importPOIs(srcfile->sqliteURI(), offlineId);
    else if(poiexport)
      exportPOIs(srcfile->fsPath().c_str(), offlineId);  // sqliteURI is read-only, use path!
  });

  return true;
}

// GUI

void MapsOffline::updateProgress(int mapid, const std::string& msg)
{
  if(!offlinePanel->isVisible()) return;
  for(Widget* item : offlineContent->select(".listitem")) {
    if(item->node->getIntAttr("__mapid") == mapid) {
      //setText("Canceling..."); setText("Download pending");
      item->selectFirst(".detail-text")->setText(msg.c_str());
      return;
    }
  }
}

void MapsOffline::populateOffline()
{
  bool hasItems = false;
  app->gui->deleteContents(offlineContent, ".listitem");
  const char* query = "SELECT mapid, lng0,lat0,lng1,lat1, source, title, timestamp, done FROM offlinemaps ORDER BY timestamp DESC;";
  SQLiteStmt(app->bkmkDB, query).exec([&](int mapid, double lng0, double lat0, double lng1, double lat1,
      std::string sourcestr, std::string titlestr, int64_t timestamp, int64_t done){
    const auto& srcinfo = app->mapsSources->mapSources[sourcestr];
    std::string detail = srcinfo ? srcinfo["title"].Scalar() : sourcestr;
    double sizemb = double(done)/1024/1024;
    int prec = sizemb > 10 ? 0 : (sizemb > 1 ? 1 : 2);
    detail.append(u8" \u2022 ").append(fstring("%.*f MB", prec, sizemb));
    detail.append(u8" \u2022 ").append(ftimestr("%F %H:%M", timestamp*1000));  //:%S
    Button* item = createListItem(
        MapsApp::uiIcon("fold-map"), titlestr.c_str(), done ? detail.c_str() : "Download pending");
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
      app->map->markerSetStylingFromPath(rectMarker, "layers.offline-region.draw.marker");
      app->map->markerSetPolyline(rectMarker, bounds, 5);
      app->lookAt(bounds[0], bounds[2]);
      if(app->mapsSources->currSource != sourcestr)
        app->mapsSources->rebuildSource(sourcestr);
    };

    Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowMenu->addItem(done ? "Delete" : "Cancel", [=](){
      if(rectMarker)
        app->map->markerSetVisible(rectMarker, false);
      if(cancelDownload(mapid)) {
        MapsOffline::queueOfflineTask(-1, [=](){ deleteOfflineMap(mapid); });
        item->selectFirst(".detail-text")->setText("Deleting...");
      }
      else
        item->selectFirst(".detail-text")->setText("Canceling...");
    });
    overflowBtn->setMenu(overflowMenu);
    container->addWidget(overflowBtn);

    offlineContent->addWidget(item);
    hasItems = true;
  });
  offlineContent->selectFirst(".empty-list-message")->setVisible(!hasItems);
  // wake up download thread to update progress
  if(!offlinePending.empty()) {
    prevProgressUpdate = 0;
    semOfflineWorker.post();
  }
}

Widget* MapsOffline::createPanel()
{
  mapsOfflineInst = this;
  maxOfflineDownloads = MapsApp::config["storage"]["offline_download_rate"].as<int>(8);
  // should we include zoom? total bytes?
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS offlinemaps(mapid INTEGER PRIMARY KEY,"
      " lng0 REAL, lat0 REAL, lng1 REAL, lat1 REAL, maxzoom INTEGER, source TEXT, title TEXT,"
      " done INTEGER DEFAULT 0, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));");

  TextBox* downloadText = new TextBox(createTextNode(""));
  downloadText->node->setAttribute("box-anchor", "left");
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
    SQLiteStmt(app->bkmkDB, query).bind(offlineId, lngLat00.longitude, lngLat00.latitude, lngLat11.longitude,
        lngLat11.latitude, maxZoom, app->mapsSources->currSource, trimStr(titleEdit->text())).exec();
    populateOffline();
    auto item = static_cast<Button*>(offlineContent->selectFirst(".listitem"));
    if(item) item->onClicked();
  };

  //Widget* downloadPanel = createInlineDialog({titleEdit, maxZoomRow}, "Download", downloadFn);  //createColumn();
  downloadDialog.reset(createInputDialog({downloadText, titleEdit, maxZoomRow}, "Download", "Start", downloadFn));

  Button* openBtn = createToolbutton(MapsApp::uiIcon("open-folder"), "Install Offline Map");
  auto openMapFn = [this](std::unique_ptr<PlatformFile> file){ openForImport(std::move(file)); };
  openBtn->onClicked = [=](){ MapsApp::openFileDialog({{"MBTiles files", "mbtiles"}}, openMapFn); };

  Button* saveBtn = createToolbutton(MapsApp::uiIcon("download"), "Save Offline Map");
  saveBtn->onClicked = [=](){
    LngLat lngLat00, lngLat11;
    app->getMapBounds(lngLat00, lngLat11);
    double ykm = lngLatDist(lngLat00, LngLat(lngLat00.longitude, lngLat11.latitude));
    double xkm = lngLatDist(lngLat11, LngLat(lngLat00.longitude, lngLat11.latitude));
    int minzoom = std::round(MapProjection::zoomAtMetersPerPixel(
        1000*std::min(ykm, xkm)/MapProjection::tileSize() ));

    std::string dltext = "Current region: " + MapsApp::distKmToStr(xkm, 1) + " x " + MapsApp::distKmToStr(ykm, 1);
    int maxZoom = 0;
    auto& tileSources = app->map->getScene()->tileSources();
    bool hasVector = false;
    for(auto& src : tileSources) {
      if(src->offlineInfo().cacheFile.empty())
        dltext += fstring("\nWarning: layer '%s' has no cache and cannot be saved", src->name().c_str());
      else
        maxZoom = std::max(maxZoom, src->maxZoom());
      hasVector = hasVector || !src->isRaster();
    }
    int maxdz = MapsApp::cfg()["storage"]["max_offline_dz"].as<int>(6);
    if(maxZoom > minzoom + maxdz) {
      maxZoom = minzoom + maxdz;
      dltext += "\nNote: max zoom has been limited to prevent excessive downloads"
          "\nChoose a smaller area or increase storage.max_offline_dz";
    }
    if(hasVector)
      dltext += "\nNote: max zoom >=14 required for offline search data.";
    downloadText->setText(dltext.c_str());
    maxZoomSpin->setLimits(1, maxZoom);
    maxZoomSpin->setValue(std::min(int(std::ceil(app->map->getZoom())) + 1, maxZoom));
    titleEdit->setText(ftimestr("%FT%H.%M.%S").c_str());

    std::string title = "Download";
    if(!app->mapsSources->currSource.empty()) {
      const auto& srcinfo = app->mapsSources->mapSources[app->mapsSources->currSource];
      if(srcinfo) { title = "Download " + srcinfo["title"].Scalar(); }
    }
    static_cast<TextLabel*>(downloadDialog->selectFirst(".dialog-title"))->setText(title.c_str());
    downloadDialog->focusedWidget = NULL;
    showModalCentered(downloadDialog.get(), MapsApp::gui);  //showInlineDialogModal(downloadPanel);
    MapsApp::gui->setFocused(titleEdit, SvgGui::REASON_TAB);
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

  SvgText* msgnode = createTextNode(
      "Zoom and pan the map to select a region, then tap the download button above.");
  msgnode->setText(SvgPainter::breakText(msgnode, 250).c_str());  //app->getPanelWidth() - 70
  msgnode->addClass("empty-list-message");
  offlineContent->addWidget(new TextBox(msgnode));

  Button* offlineBtn = createToolbutton(MapsApp::uiIcon("offline"), "Offline Maps");
  offlineBtn->onClicked = [this](){
    app->showPanel(offlinePanel, true);
    populateOffline();
  };
  return offlineBtn;
}
