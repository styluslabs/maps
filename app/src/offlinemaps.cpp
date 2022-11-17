#include "offlinemaps.h"
#include "mapsapp.h"
#include "util.h"
#include "imgui.h"
#include <deque>
// "private" headers
#include "scene/scene.h"
#include "data/mbtilesDataSource.h"
#include "data/networkDataSource.h"

using namespace Tangram;

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
  std::vector<std::string> urlSubdomains;
  bool urlIsTms;
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
    std::unique_ptr<MBTilesDataSource> mbtiles;
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
  mbtiles = std::make_unique<MBTilesDataSource>(_platform, src.name, src.cacheFile, "", true);
  NetworkDataSource::UrlOptions urlOptions = {src.urlSubdomains, src.urlIsTms};
  mbtiles->next = std::make_unique<NetworkDataSource>(_platform, src.url, urlOptions);
  name = src.name + "-" + std::to_string(ofl.id);
  offlineId = ofl.id;
  searchData = parseSearchFields(src.searchData);

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
}

bool OfflineDownloader::fetchNextTile()
{
  std::unique_lock<std::mutex> lock(m_mutexQueue);
  if(m_queued.empty()) return false;
  auto task = std::make_shared<BinaryTileTask>(m_queued.front(), nullptr);
  task->offlineId = offlineId;
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

    if(task->tileId().z == srcMaxZoom && !searchData.empty()) {
      indexTileData(task, offlineId, searchData);
    }

    LOGW("%s: completed download of offline tile %s", name.c_str(), task->tileId().toString().c_str());
  }
  m_pending.erase(pendingit);

  semOfflineWorker.post();
}

void MapsOffline::showGUI()
{
  static int maxZoom = 13;
  if (!ImGui::CollapsingHeader("Offline Maps"))  //, ImGuiTreeNodeFlags_DefaultOpen))
    return;

  Map* map = app->map;
  ImGui::InputInt("Max zoom level", &maxZoom);
  if (ImGui::Button("Save Offline Map") && maxZoom > 0 && maxZoom < 20) {
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
        offlinePending.back().sources.push_back({src->name(), info.cacheFile, info.url,
            info.urlSubdomains, info.urlIsTms, src->maxZoom()});
        if(!src->isRaster())
          YamlPath("global.search_data").get(map->getScene()->config(), offlinePending.back().sources.back().searchData);
      }
    }

    MapsApp::platform->onUrlRequestsThreshold = [&](){ semOfflineWorker.post(); };  //onUrlClientIdle;
    MapsApp::platform->urlRequestsThreshold = maxOfflineDownloads - 1;
    semOfflineWorker.post();
    runOfflineWorker = true;
    if(!offlineWorker)
      offlineWorker = std::make_unique<std::thread>(offlineDLMain);
  }
}

MapsOffline::~MapsOffline()
{
  if(offlineWorker) {
    runOfflineWorker = false;
    semOfflineWorker.post();
    offlineWorker->join();
  }
}
