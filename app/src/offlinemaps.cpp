#include "offlinemaps.h"
#include "mapsapp.h"
#include "mapsearch.h"
#include "util.h"
//#include "imgui.h"
#include <deque>
#include "sqlite3/sqlite3.h"
// "private" headers
#include "scene/scene.h"
#include "data/mbtilesDataSource.h"
#include "data/networkDataSource.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"

#include "mapsources.h"

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

// We'll need to resume offline downloads after app or device restart, so use easily serializable data format
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
    bool fetchNextTile();
    void cancel();
    std::string name;
    int totalTiles = 0;

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
static std::deque<OfflineMapInfo> offlinePending;
static std::mutex mutexOfflineQueue;
static std::vector<std::unique_ptr<OfflineDownloader>> offlineDownloaders;

static void offlineDLStep()
{
  std::unique_lock<std::mutex> lock(mutexOfflineQueue);

  Platform& platform = *MapsApp::platform;
  while(!offlinePending.empty()) {
    if(offlineDownloaders.empty()) {
      auto& dl = offlinePending.front();
      for(auto& source : dl.sources)
        offlineDownloaders.emplace_back(new OfflineDownloader(platform, dl, source));
    }
    while(!offlineDownloaders.empty()) {
      // DB access (and this network requests for missing tiles) are async, so activeUrlRequests() won't update
      int nreq = maxOfflineDownloads - int(platform.activeUrlRequests());
      while(nreq > 0 && offlineDownloaders.back()->fetchNextTile()) --nreq;
      if(nreq <= 0 || offlineDownloaders.back()->remainingTiles())
        return;  // m_queued empty, m_pending not empty
      LOGD("completed offline tile downloads for layer %s", offlineDownloaders.back()->name.c_str());
      offlineDownloaders.pop_back();
    }
    LOG("completed offline tile downloads for map %d", offlinePending.front().id);

    MapsApp::runOnMainThread([id=offlinePending.front().id, canceled=offlinePending.front().canceled](){
      mapsOfflineInst->downloadCompleted(id, canceled);
    });

    offlinePending.pop_front();
  }
  platform.onUrlRequestsThreshold = nullptr;  // all done!
}

static void offlineDLMain()
{
  semOfflineWorker.wait();
  while(runOfflineWorker) {
    offlineDLStep();
    semOfflineWorker.wait();
  }
}

OfflineDownloader::OfflineDownloader(Platform& _platform, const OfflineMapInfo& ofl, const OfflineSourceInfo& src)
{
  mbtiles = std::make_unique<Tangram::MBTilesDataSource>(_platform, src.name, src.cacheFile, "", true);
  mbtiles->next = std::make_unique<Tangram::NetworkDataSource>(_platform, src.url, src.urlOptions);
  name = src.name + "-" + std::to_string(ofl.id);
  offlineId = ofl.id;
  searchData = MapsSearch::parseSearchFields(src.searchData);
  offlineSize = mbtiles->getOfflineSize();

  srcMaxZoom = std::min(ofl.maxZoom, src.maxZoom);
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

bool OfflineDownloader::fetchNextTile()
{
  std::unique_lock<std::mutex> lock(m_mutexQueue);
  if(m_queued.empty()) return false;
  auto task = std::make_shared<BinaryTileTask>(m_queued.front(), nullptr);
  task->offlineId = searchData.empty() ? offlineId : -offlineId;
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
  auto pendingit = std::find(m_pending.begin(), m_pending.begin(), tileId);
  if(pendingit == m_pending.end()) {
    LOGW("Pending tile entry not found for tile!");
    return;
  }
  // put back in queue (at end) on failure
  if(canceled) {}
  else if(!task->hasData()) {
    m_queued.push_back(*pendingit);
    LOGW("%s: download of offline tile %s failed - will retry", name.c_str(), task->tileId().toString().c_str());
  } else {
    if(!searchData.empty() && task->tileId().z == srcMaxZoom)
      MapsSearch::indexTileData(task.get(), offlineId, searchData);
    LOGD("%s: completed download of offline tile %s", name.c_str(), task->tileId().toString().c_str());
  }
  m_pending.erase(pendingit);

  MapsApp::runOnMainThread([](){
    mapsOfflineInst->updateProgress();
  });

  semOfflineWorker.post();
}

void MapsOffline::saveOfflineMap(int mapid, LngLat lngLat00, LngLat lngLat11, int maxZoom)
{
  Map* map = app->map;
  std::unique_lock<std::mutex> lock(mutexOfflineQueue);
  // don't load tiles outside visible region at any zoom level (as using TileID::getChild() recursively
  //  would do - these could potentially outnumber the number of desired tiles!)
  int zoom = int(map->getZoom());
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

  MapsApp::platform->onUrlRequestsThreshold = [&](){ semOfflineWorker.post(); };  //onUrlClientIdle;
  MapsApp::platform->urlRequestsThreshold = maxOfflineDownloads - 1;
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
  return offlinePending.size();
}

bool MapsOffline::cancelDownload(int mapid)
{
  std::unique_lock<std::mutex> lock(mutexOfflineQueue);
  if(offlinePending.front().id == mapid) {
    offlinePending.front().canceled = true;
    for(auto& dl : offlineDownloaders)
      dl->cancel();
    return false;
  }
  std::remove_if(offlinePending.begin(), offlinePending.end(), [mapid](auto a){ a.id == mapid; });
  return true;
}

static void deleteOfflineMap(int mapid)
{
  int64_t offlineSize = 0;
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
    s->deleteOfflineMap(mapid);
    offlineSize += s->getOfflineSize();
  }
  MapsApp::platform->notifyStorage(0, offlineSize);  // this can trigger cache shrink, so wait until all sources processed

  DB_exec(MapsApp::bkmkDB, "DELETE FROM offlinemaps WHERE mapid = ?;", NULL,
      [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, mapid); });
}

void MapsOffline::downloadCompleted(int id, bool canceled)
{
  if(canceled) {
    deleteOfflineMap(id);
  }
  else {
    DB_exec(MapsApp::bkmkDB, "UPDATE offlinemaps SET done = 1 WHERE mapid = ?;", NULL,
        [&](sqlite3_stmt* stmt){ sqlite3_bind_int(stmt, 1, id); });
  }
  populateOffline();
}

void MapsOffline::resumeDownloads()
{
  // caller should restore map source if necessary
  //std::string prevsrc = app->mapsSources->currSource;
  const char* query = "SELECT mapid, lng0,lat0,lng1,lat1, source FROM offlinemaps WHERE done = 0 ORDER BY timestamp;";
  DB_exec(app->bkmkDB, query, [&](sqlite3_stmt* stmt){
    int mapid = sqlite3_column_int(stmt, 0);
    double lng0 = sqlite3_column_double(stmt, 1), lat0 = sqlite3_column_double(stmt, 2);
    double lng1 = sqlite3_column_double(stmt, 3), lat1 = sqlite3_column_double(stmt, 4);
    std::string sourcestr = (const char*)(sqlite3_column_text(stmt, 5));

    app->mapsSources->rebuildSource(sourcestr);
    saveOfflineMap(mapid, LngLat(lng0, lat0), LngLat(lng1, lat1));

    LOG("Resuming offline map download for source %s", sourcestr.c_str());
  });
}

// GUI

void MapsOffline::updateProgress()
{
  if(!offlinePanel->isVisible()) return;
  std::unique_lock<std::mutex> lock(mutexOfflineQueue);

  for(Widget* item : offlineContent->select(".listitem")) {
    int mapid = item->node->getIntAttr("__mapid");
    for(size_t ii = 0; ii < offlinePending.size(); ++ii) {
      if(offlinePending[ii].id == mapid) {
        if(offlinePending[ii].canceled)
          item->selectFirst(".detail-text")->setText("Canceling...");
        else if(ii == 0) {
          int total = 0, remaining = 0;
          for(auto& dl : offlineDownloaders) {
            total += dl->totalTiles;
            remaining += dl->remainingTiles();
          }
          item->selectFirst(".detail-text")->setText(fstring("%d/%d tiles downloaded", total - remaining, total).c_str());
        }
        else
          item->selectFirst(".detail-text")->setText("Download pending");
        item->selectFirst(".delete-btn")->setText("Cancel");
        break;
      }
    }
  }
}

void MapsOffline::populateOffline()
{
  static const char* offlineListProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g layout="flex" flex-direction="row" box-anchor="hfill">
        <g class="image-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/ui-icons.svg#fold-map"/>
        </g>
        <g layout="box" box-anchor="vfill">
          <text class="title-text" box-anchor="left" margin="0 10"></text>
          <text class="detail-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
        </g>

        <rect class="stretch" fill="none" box-anchor="fill" width="20" height="20"/>

        <g class="toolbutton delete-btn" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/ui-icons.svg#discard"/>
        </g>

      </g>
    </g>
  )";
  std::unique_ptr<SvgNode> offlineListProto;
  if(!offlineListProto)
    offlineListProto.reset(loadSVGFragment(offlineListProtoSVG));

  app->gui->deleteContents(offlineContent, ".listitem");

  const char* query = "SELECT mapid, lng0,lat0,lng1,lat1, source, title FROM offlinemaps ORDER BY timestamp DESC;";
  DB_exec(app->bkmkDB, query, [&](sqlite3_stmt* stmt){
    int mapid = sqlite3_column_int(stmt, 0);
    double lng0 = sqlite3_column_double(stmt, 1), lat0 = sqlite3_column_double(stmt, 2);
    double lng1 = sqlite3_column_double(stmt, 3), lat1 = sqlite3_column_double(stmt, 4);
    std::string sourcestr = (const char*)(sqlite3_column_text(stmt, 5));
    std::string titlestr = (const char*)(sqlite3_column_text(stmt, 6));

    Button* item = new Button(offlineListProto->clone());
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

    Button* deleteBtn = new Button(item->containerNode()->selectFirst(".delete-btn"));
    deleteBtn->onClicked = [=](){
      if(rectMarker)
        app->map->markerSetVisible(rectMarker, false);
      if(cancelDownload(mapid)) {
        deleteOfflineMap(mapid);
        populateOffline();
      }
      else
        updateProgress();
    };

    SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
    titlenode->addText(titlestr.c_str());
    SvgText* detailnode = static_cast<SvgText*>(item->containerNode()->selectFirst(".detail-text"));
    auto srcinfo = app->mapsSources->mapSources[sourcestr];
    detailnode->addText(srcinfo ? srcinfo["title"].Scalar().c_str() : sourcestr.c_str());

    offlineContent->addWidget(item);
  });
  updateProgress();
}

Widget* MapsOffline::createPanel()
{
  mapsOfflineInst = this;
  // should we include zoom? total bytes?
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS offlinemaps(mapid INTEGER PRIMARY KEY,"
      " lng0 REAL, lat0 REAL, lng1 REAL, lat1 REAL, source TEXT, title TEXT, done INTEGER DEFAULT 0,"
      " timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));");

  TextEdit* titleEdit = createTextEdit();
  SpinBox* maxZoomSpin = createSpinBox(13, 1, 1, 20, "%.0f");
  Button* confirmBtn = createPushbutton("Download");
  Button* cancelBtn = createPushbutton("Cancel");

  Widget* downloadPanel = createColumn();
  downloadPanel->addWidget(createTitledRow("Title", titleEdit));
  downloadPanel->addWidget(createTitledRow("Max zoom", maxZoomSpin));
  downloadPanel->addWidget(createTitledRow(NULL, confirmBtn, cancelBtn));
  downloadPanel->setVisible(false);

  confirmBtn->onClicked = [=](){
    LngLat lngLat00, lngLat11;
    app->getMapBounds(lngLat00, lngLat11);
    int offlineId = int(time(NULL));
    saveOfflineMap(offlineId, lngLat00, lngLat11, int(maxZoomSpin->value()));

    const char* query = "INSERT INTO offlinemaps (mapid,lng0,lat0,lng1,lat1,source,title) VALUES (?,?,?,?,?,?,?);";
    DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_int(stmt, 1, offlineId);
      sqlite3_bind_double(stmt, 2, lngLat00.longitude);
      sqlite3_bind_double(stmt, 3, lngLat00.latitude);
      sqlite3_bind_double(stmt, 4, lngLat11.longitude);
      sqlite3_bind_double(stmt, 5, lngLat11.latitude);
      sqlite3_bind_text(stmt, 6, app->mapsSources->currSource.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 7, titleEdit->text().c_str(), -1, SQLITE_TRANSIENT);
    });

    downloadPanel->setVisible(false);
    populateOffline();
    auto item = static_cast<Button*>(offlineContent->selectFirst(".listitem"));
    if(item) item->onClicked();
  };

  cancelBtn->onClicked = [=](){ downloadPanel->setVisible(false); };

  Button* saveBtn = createToolbutton(MapsApp::uiIcon("download"), "Save Offline Map");
  saveBtn->onClicked = [=](){
    char timestr[64];
    time_t t = mSecSinceEpoch()/1000;
    strftime(timestr, sizeof(timestr), "%FT%H.%M.%S", localtime(&t));  //"%Y-%m-%d %HH%M"
    titleEdit->setText(timestr);

    int maxZoom = 0;
    auto& tileSources = app->map->getScene()->tileSources();
    for(auto& src : tileSources)
      maxZoom = std::max(maxZoom, src->maxZoom());
    maxZoomSpin->setLimits(1, maxZoom);
    if(maxZoomSpin->value() > maxZoom)
      maxZoomSpin->setValue(maxZoom);

    downloadPanel->setVisible(true);
  };

  offlineContent = createColumn();
  auto toolbar = app->createPanelHeader(MapsApp::uiIcon("offline"), "Offline Maps");
  toolbar->addWidget(createStretch());
  //toolbar->addWidget(maxZoomSpin);
  toolbar->addWidget(saveBtn);
  offlinePanel = app->createMapPanel(toolbar, offlineContent);

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
