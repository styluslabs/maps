#include "offlinemaps.h"
#include "mapsapp.h"
#include "mapsearch.h"
#include "util.h"
#include "imgui.h"
#include <deque>
#include "sqlite3/sqlite3.h"
// "private" headers
#include "scene/scene.h"
#include "data/mbtilesDataSource.h"
#include "data/networkDataSource.h"

static bool runOfflineWorker = false;
static std::unique_ptr<std::thread> offlineWorker;
static Semaphore semOfflineWorker(1);


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
};

class OfflineDownloader
{
public:
    OfflineDownloader(Platform& _platform, const OfflineMapInfo& ofl, const OfflineSourceInfo& src);
    size_t remainingTiles() const { return m_queued.size() + m_pending.size(); }
    bool fetchNextTile();
    std::string name;

private:
    void tileTaskCallback(std::shared_ptr<TileTask> _task);

    int offlineId;
    int srcMaxZoom;
    std::deque<TileID> m_queued;
    std::vector<TileID> m_pending;
    std::mutex m_mutexQueue;
    std::unique_ptr<Tangram::MBTilesDataSource> mbtiles;
    std::vector<SearchData> searchData;
};

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
      LOGW("completed offline tile downloads for layer %s", offlineDownloaders.back()->name.c_str());
      offlineDownloaders.pop_back();
    }
    LOGW("completed offline tile downloads for map %d", offlinePending.front().id);
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
}

bool OfflineDownloader::fetchNextTile()
{
  std::unique_lock<std::mutex> lock(m_mutexQueue);
  if(m_queued.empty()) return false;
  auto task = std::make_shared<Tangram::BinaryTileTask>(m_queued.front(), nullptr);
  task->offlineId = searchData.empty() ? offlineId : -offlineId;
  m_pending.push_back(m_queued.front());
  m_queued.pop_front();
  lock.unlock();
  TileTaskCb cb{[this](std::shared_ptr<TileTask> _task) { tileTaskCallback(_task); }};
  mbtiles->loadTileData(task, cb);
  LOGW("%s: requested download of offline tile %s", name.c_str(), task->tileId().toString().c_str());
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
  if(!task->hasData()) {
    m_queued.push_back(*pendingit);
    LOGW("%s: download of offline tile %s failed - will retry", name.c_str(), task->tileId().toString().c_str());
  } else {
    if(!searchData.empty() && task->tileId().z == srcMaxZoom)
      MapsSearch::indexTileData(task.get(), offlineId, searchData);
    LOGW("%s: completed download of offline tile %s", name.c_str(), task->tileId().toString().c_str());
  }
  m_pending.erase(pendingit);

  semOfflineWorker.post();
}

void MapsOffline::saveOfflineMap(int maxZoom)
{
  Map* map = app->map;
  std::unique_lock<std::mutex> lock(mutexOfflineQueue);
  // don't load tiles outside visible region at any zoom level (as using TileID::getChild() recursively
  //  would do - these could potentially outnumber the number of desired tiles!)
  int zoom = int(map->getZoom());
  LngLat lngLat00, lngLat11;
  app->getMapBounds(lngLat00, lngLat11);
  // queue offline downloads
  int offlineId = (unsigned)time(NULL);
  offlinePending.push_back({offlineId, lngLat00, lngLat11, zoom, maxZoom, {}});
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

  const char* query = "INSERT INTO offlinemaps (id,lng0,lat0,lng1,lat1) VALUES (?,?,?,?,?);";
  DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
    sqlite3_bind_int(stmt, 1, offlineId);
    sqlite3_bind_double(stmt, 2, lngLat00.longitude);
    sqlite3_bind_double(stmt, 3, lngLat00.latitude);
    sqlite3_bind_double(stmt, 4, lngLat11.longitude);
    sqlite3_bind_double(stmt, 5, lngLat11.latitude);
  });
}

void MapsOffline::showGUI()
{
  static int maxZoom = 13;
  if (!ImGui::CollapsingHeader("Offline Maps"))  //, ImGuiTreeNodeFlags_DefaultOpen))
    return;

  ImGui::InputInt("Max zoom level", &maxZoom);
  if(ImGui::Button("Save Offline Map") && maxZoom > 0 && maxZoom < 20)
    saveOfflineMap(maxZoom);
}

MapsOffline::~MapsOffline()
{
  if(offlineWorker) {
    runOfflineWorker = false;
    semOfflineWorker.post();
    offlineWorker->join();
  }
}

// New GUI

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"

#include "mapsources.h"

Widget* MapsOffline::createPanel()
{
  static const char* offlineListProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g layout="flex" flex-direction="row" box-anchor="left">
        <g class="image-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_bookmark.svg"/>
        </g>
        <g layout="box" box-anchor="vfill">
          <text class="title-text" box-anchor="left" margin="0 10"></text>
          <text class="detail-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
        </g>

        <g class="toolbutton delete-btn" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_discard.svg"/>
        </g>

      </g>
    </g>
  )";
  offlineListProto.reset(loadSVGFragment(offlineListProtoSVG));

  // should we include zoom? total bytes?
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS offlinemaps(mapid INTEGER, lng0 REAL, lat0 REAL, lng1 REAL, lat1 REAL, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);");

  SpinBox* maxZoomSpin = createSpinBox(13, 1, 1, 20, "%d");
  Button* saveBtn = createPushbutton("Save Offline Map");

  saveBtn->onClicked = [=](){
    saveOfflineMap(maxZoomSpin->value());
  };

  auto offlineContent = createColumn();
  auto toolbar = createToolbar();
  toolbar->addWidget(app->createHeaderTitle(SvgGui::useFile(":/icons/ic_menu_cloud.svg"), "Offline Maps"));
  toolbar->addWidget(createStretch());
  toolbar->addWidget(maxZoomSpin);
  toolbar->addWidget(saveBtn);
  auto offlineHeader = app->createPanelHeader(toolbar);
  offlinePanel = app->createMapPanel(offlineContent, offlineHeader);

  // we need list of existing offline regions - basically save OfflineMapInfo to places.sqlite?
  // how to identify offline maps in list? date? sources?

  DB_exec(app->bkmkDB, "SELECT mapid, lng0,lat0,lng1,lat1, strftime('%x', timestamp) FROM offlinemaps;", [=](sqlite3_stmt* stmt){
    int mapid = sqlite3_column_int(stmt, 0);
    double lng0 = sqlite3_column_double(stmt, 1), lat0 = sqlite3_column_double(stmt, 2);
    double lng1 = sqlite3_column_double(stmt, 3), lat1 = sqlite3_column_double(stmt, 4);
    const char* titlestr = (const char*)(sqlite3_column_text(stmt, 5));

    Button* item = new Button(offlineListProto->clone());
    item->onClicked = [=](){
      // show bounds of offline region on map
      LngLat bounds[5] = {{lng0, lat0}, {lng0, lat1}, {lng1, lat1}, {lng1, lat0}, {lng0, lat0}};
      if(!rectMarker)
        rectMarker = app->map->markerAdd();
      //app->map->markerSetStylingFromString(polyline_marker, polylineStyle.c_str());
      app->map->markerSetPolyline(rectMarker, bounds, 5);
      app->map->setCameraPositionEased(app->map->getEnclosingCameraPosition(bounds[0], bounds[2]), 0.5f);
    };

    Button* deleteBtn = new Button(item->containerNode()->selectFirst(".delete-btn"));
    deleteBtn->onClicked = [=](){

    };

    SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
    titlenode->addText(titlestr);
    offlineContent->addWidget(item);
  });

  Button* offlineBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_expanddown.svg"), "Offline Maps");
  offlineBtn->onClicked = [this](){
    app->showPanel(offlinePanel);
  };
  return offlineBtn;
}
