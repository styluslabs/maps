// UI automation (`ascend --automation <path>`, tests): linked only into desktop dev builds (AUTOMATION=1 -
//  default except for release packages; see Makefile).  MapsApp calls this code only through hooks set by the
//  static initializer below, so the app links w/o it.  Commands: ugui's UiDriver (dump, click, type, wait, ...)
//  plus the map commands below; see `help`.

#include "mapsapp.h"
#include "mapsources.h"
#include "util.h"
#include "ugui/svggui_platform.h"
#include "ugui/uidriver.h"
#include "scene/scene.h"
#include "style/style.h"
#include "tile/tile.h"
#include "tile/tileManager.h"
#include "labels/labelManager.h"
#include "util/elevationManager.h"
#include "util/asyncWorker.h"
#include <thread>
#include <chrono>
#include <memory>
#include <atomic>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// provided by platform main (linuxmain.cpp): save frame just drawn (called before buffer swap) as PNG; w <= 0
//  for whole window
bool screenshotPng(const char* path, int x, int y, int w, int h);

using Tangram::SceneUpdate;
typedef UiDriver::CommandResult CmdRes;
typedef std::vector<std::string> Args;

namespace {

struct MapAutomation
{
  std::unique_ptr<UiDriver> driver;
  FILE* replyOut = NULL;
  int framesDrawn = 0;  // frames drawn since start
  std::vector<SceneUpdate> sceneUpdates;  // from scene command; reapplied after source change

  // state for pending commands
  bool pending = false;
  int pendingStartFrame = 0;
  Timestamp pendingUntil = 0;
  int completeFrames = 0;
  int lastCheckedFrame = 0;
  std::shared_ptr<std::atomic<int>> workersIdle;  // set by marker tasks queued on async workers

  void start(const std::string& path);
  void poll(bool frameDrawn);
  void addCommands();
  void applySceneUpdates();
  bool viewComplete();
  std::string dumpMap();
  void beginPending(double timeoutSecs);
  void endPending() { pending = false; workersIdle.reset(); }
};

MapAutomation* automation = NULL;

std::string mapStateStr(Tangram::MapState s)
{
  std::string res = fstring("0x%x", s.flags);
  if(s.viewChanging()) res += " view_changing";
  if(s.labelsChanging()) res += " labels_changing";
  if(s.tilesLoading()) res += " tiles_loading";
  if(s.sceneLoading()) res += " scene_loading";
  if(s.isAnimating()) res += " is_animating";
  return res;
}

void MapAutomation::beginPending(double timeoutSecs)
{
  pending = true;
  pendingStartFrame = framesDrawn;
  pendingUntil = mSecSinceEpoch() + Timestamp(1000*timeoutSecs);
  completeFrames = 0;
  lastCheckedFrame = framesDrawn;
  workersIdle.reset();
  MapsApp::platform->requestRender();
}

// view is complete when map reports complete, no tile tasks are in flight for two frames in a row, and tasks
//  queued on the terrain offscreen worker and Map's async worker have run (as in bench replay waitComplete)
bool MapAutomation::viewComplete()
{
  MapsApp* app = MapsApp::inst;
  if(framesDrawn == lastCheckedFrame) return false;  // only check after a new frame
  lastCheckedFrame = framesDrawn;
  auto* scene = app->map->getScene();
  bool complete = app->mapState.viewComplete() && scene && !scene->tileManager()->tasksInFlight();
  completeFrames = complete ? completeFrames + 1 : 0;
  if(!complete) { workersIdle.reset(); }
  if(completeFrames < 2) return false;
  if(!workersIdle) {
    // count reaches 0 when all marker tasks have run
    auto& worker = Tangram::ElevationManager::offscreenWorker;
    auto idle = std::make_shared<std::atomic<int>>(worker ? -2 : -1);
    workersIdle = idle;
    if(worker) { worker->enqueue([idle](){ ++*idle; }); }
    app->map->runAsyncTask([idle](){ ++*idle; });
    return false;
  }
  return *workersIdle >= 0;
}

// StyledMesh::numVertices()/numIndices() are recent; detect them so automation builds w/ older tangram (e.g.
//  baseline builds for testing/shots/shots.py)
template<class M> auto meshVertices(const M& m, int) -> decltype(m.numVertices()) { return m.numVertices(); }
template<class M> size_t meshVertices(const M&, long) { return 0; }
template<class M> auto meshIndices(const M& m, int) -> decltype(m.numIndices()) { return m.numIndices(); }
template<class M> size_t meshIndices(const M&, long) { return 0; }

std::string MapAutomation::dumpMap()
{
  MapsApp* app = MapsApp::inst;
  auto pos = app->map->getCameraPosition();
  std::string out = fstring("camera %.7f %.7f %.3f rotation %.2f tilt %.2f\n", pos.longitude, pos.latitude,
      pos.zoom, pos.rotation*180/M_PI, pos.tilt*180/M_PI);
  out += "state " + mapStateStr(app->mapState) + "\n";
  out += fstring("frames %d\n", framesDrawn);
  auto* scene = app->map->getScene();
  if(!scene) return out + "no scene\n";
  auto* tm = scene->tileManager();
  if(!tm) return out + "no tile manager\n";
  const auto& tiles = tm->getVisibleTiles();
  out += fstring("tiles visible %d loading %d tasks_in_flight %d labels %d\n", int(tiles.size()),
      tm->numLoadingTiles(), int(tm->tasksInFlight()), int(scene->labelManager()->numLabels()));
  struct Stats { size_t meshes = 0, vertices = 0, indices = 0, bytes = 0; };
  std::map<std::string, Stats> styleStats;
  Stats total;
  for(const auto& tile : tiles) {
    for(const auto& style : scene->styles()) {
      const auto& mesh = tile->getMesh(*style);
      if(!mesh) continue;
      Stats& s = styleStats[style->getName()];
      for(Stats* st : {&s, &total}) {
        st->meshes++;
        st->vertices += meshVertices(*mesh, 0);
        st->indices += meshIndices(*mesh, 0);
        st->bytes += mesh->bufferSize();
      }
    }
  }
  out += "style meshes vertices indices bytes (visible tiles)\n";
  for(const auto& ss : styleStats) {
    const Stats& s = ss.second;
    out += fstring("  %s %d %d %d %d\n", ss.first.c_str(), int(s.meshes), int(s.vertices), int(s.indices),
        int(s.bytes));
  }
  out += fstring("  total %d %d %d %d\n", int(total.meshes), int(total.vertices), int(total.indices),
      int(total.bytes));
  return out;
}

void MapAutomation::applySceneUpdates()
{
  MapsApp* app = MapsApp::inst;
  for(const SceneUpdate& upd : sceneUpdates) {
    auto& upds = app->sceneUpdates;
    upds.erase(std::remove_if(upds.begin(), upds.end(),
        [&](const SceneUpdate& u){ return u.path == upd.path; }), upds.end());
    upds.push_back(upd);
  }
  app->loadSceneFile();
}

void MapAutomation::addCommands()
{
  UiDriver* d = driver.get();
  MapsApp* app = MapsApp::inst;

  d->addCommand("view", "view <lng> <lat> <zoom> [rotation] [tilt] - set camera (degrees; no animation)",
      [=](const Args& args, std::string& status, std::string& out) {
    if(args.size() < 4 || args.size() > 6) { status = "error: wrong arguments"; return UiDriver::CMD_DONE; }
    auto pos = app->map->getCameraPosition();
    pos.longitude = atof(args[1].c_str());
    pos.latitude = atof(args[2].c_str());
    pos.zoom = float(atof(args[3].c_str()));
    pos.rotation = args.size() > 4 ? float(atof(args[4].c_str())*M_PI/180) : 0;
    pos.tilt = args.size() > 5 ? float(atof(args[5].c_str())*M_PI/180) : 0;
    app->followState = MapsApp::NO_FOLLOW;
    app->map->setCameraPosition(pos);
    MapsApp::platform->requestRender();
    return UiDriver::CMD_DONE;
  });

  d->addCommand("scene", "scene <path> <value> - scene update (kept across source changes), e.g."
      " scene layers.roads.draw.lines.width 12px", [=](const Args& args, std::string& status, std::string& out) {
    if(args.size() != 3) { status = "error: wrong arguments"; return UiDriver::CMD_DONE; }
    auto it = std::find_if(sceneUpdates.begin(), sceneUpdates.end(),
        [&](const SceneUpdate& u){ return u.path == args[1]; });
    if(it != sceneUpdates.end()) { it->value = args[2]; }
    else { sceneUpdates.push_back(SceneUpdate{args[1], args[2]}); }
    applySceneUpdates();
    return UiDriver::CMD_DONE;
  });

  d->addCommand("source", "source <name> - switch map source (key in mapsources.yaml)",
      [=](const Args& args, std::string& status, std::string& out) {
    if(args.size() != 2) { status = "error: wrong arguments"; return UiDriver::CMD_DONE; }
    app->mapsSources->rebuildSource(args[1]);
    if(!sceneUpdates.empty()) { applySceneUpdates(); }
    return UiDriver::CMD_DONE;
  });

  d->addCommand("debug", "debug <flag> on|off - Tangram debug flag: freeze_tiles, proxy_colors, tile_bounds,"
      " labels, tangram_infos, draw_all_labels, tangram_stats, selection_buffer, depth_buffer",
      [=](const Args& args, std::string& status, std::string& out) {
    static const char* flags[] = {"freeze_tiles", "proxy_colors", "tile_bounds", "labels", "tangram_infos",
        "draw_all_labels", "tangram_stats", "selection_buffer", "depth_buffer"};
    if(args.size() != 3 || (args[2] != "on" && args[2] != "off")) {
      status = "error: wrong arguments";
      return UiDriver::CMD_DONE;
    }
    for(int ii = 0; ii < int(sizeof(flags)/sizeof(flags[0])); ++ii) {
      if(args[1] != flags[ii]) { continue; }
      Tangram::setDebugFlag(Tangram::DebugFlags(ii), args[2] == "on");
      // tile bounds are drawn w/ debug styles added at scene load
      if(ii == Tangram::DebugFlags::tile_bounds) { app->loadSceneFile(); }
      MapsApp::platform->requestRender();
      return UiDriver::CMD_DONE;
    }
    status = "error: unknown flag " + args[1];
    return UiDriver::CMD_DONE;
  });

  // `wait complete`; other wait commands are passed on to UiDriver
  d->addCommand("wait", "wait complete [secs] - until map view is complete (tiles and labels loaded)"
      " after at least one frame (default timeout 60 s)",
      [=](const Args& args, std::string& status, std::string& out) {
    if(args.size() < 2 || args[1] != "complete") { return UiDriver::CMD_UNHANDLED; }
    if(!pending) { beginPending(args.size() > 2 ? atof(args[2].c_str()) : 60); }
    MapsApp::platform->requestRender();  // app doesn't redraw while idle
    if(framesDrawn > pendingStartFrame && viewComplete()) {
      endPending();
      return UiDriver::CMD_DONE;
    }
    if(mSecSinceEpoch() > pendingUntil) {
      endPending();
      status = "error: timed out";
      out = dumpMap();
      return UiDriver::CMD_DONE;
    }
    return UiDriver::CMD_PENDING;
  });

  d->addCommand("screenshot", "screenshot <file.png> [x y w h] - save next frame (or region, in pixels from"
      " top left)", [=](const Args& args, std::string& status, std::string& out) {
    if(args.size() != 2 && args.size() != 6) { status = "error: wrong arguments"; return UiDriver::CMD_DONE; }
    if(!pending) { beginPending(10); }
    if(framesDrawn == pendingStartFrame) {
      MapsApp::platform->requestRender();
      if(mSecSinceEpoch() <= pendingUntil) { return UiDriver::CMD_PENDING; }
      endPending();
      status = "error: timed out waiting for frame";
      return UiDriver::CMD_DONE;
    }
    endPending();
    int x = 0, y = 0, w = 0, h = 0;
    if(args.size() == 6) {
      x = atoi(args[2].c_str()); y = atoi(args[3].c_str()); w = atoi(args[4].c_str()); h = atoi(args[5].c_str());
    }
    if(!screenshotPng(args[1].c_str(), x, y, w, h)) { status = "error: unable to write " + args[1]; }
    return UiDriver::CMD_DONE;
  });

  // `dump map`; other dump commands are passed on to UiDriver
  d->addCommand("dump", "dump map - camera, map state, tile counts, mesh stats per style (visible tiles)",
      [=](const Args& args, std::string& status, std::string& out) {
    if(args.size() != 2 || args[1] != "map") { return UiDriver::CMD_UNHANDLED; }
    out = dumpMap();
    return UiDriver::CMD_DONE;
  });
}

// drive app w/ text commands (see UiDriver::queueCommand) read from path (e.g. a FIFO, reopened after each
//  writer closes it) or stdin if path is "-"; replies are written to path + ".out" (stdout for stdin)
void MapAutomation::start(const std::string& path)
{
  if(path == "-")
    replyOut = stdout;
  else {
    struct stat st;
    if(stat(path.c_str(), &st) != 0 && mkfifo(path.c_str(), 0600) != 0) {
      PLATFORM_LOG("Unable to create FIFO %s\n", path.c_str());
      return;
    }
    replyOut = fopen((path + ".out").c_str(), "w");
    if(!replyOut) {
      PLATFORM_LOG("Unable to open %s.out\n", path.c_str());
      return;
    }
  }
  driver.reset(new UiDriver(MapsApp::gui));
  driver->modalTimeout = 0;  // dialogs stay open until commands close them (`set timeout <secs>` to change)
  FILE* out = replyOut;
  driver->replyFn = [out](const std::string& r){ fputs(r.c_str(), out); fflush(out); };
  driver->onQuit = [](){ MapsApp::runApplication = false; };
  addCommands();
  UiDriver* d = driver.get();
  // reader thread blocks in open/read, so it is detached (process exit ends it)
  std::thread([d, path](){
    for(;;) {
      FILE* in = path == "-" ? stdin : fopen(path.c_str(), "r");  // blocks until a writer opens FIFO
      if(!in) return;
      char buf[4096];
      while(fgets(buf, sizeof(buf), in))
        d->queueCommand(buf);
      if(in == stdin) return;
      fclose(in);
    }
  }).detach();
  // event loop doesn't wake w/o input events, so wake periodically for wait/sleep commands
  std::thread([](){
    for(;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      PLATFORM_WakeEventLoop();
    }
  }).detach();
  PLATFORM_LOG("UI automation: reading commands from %s\n", path == "-" ? "stdin" : path.c_str());
}

void MapAutomation::poll(bool frameDrawn)
{
  if(frameDrawn) { ++framesDrawn; }
  if(driver) { driver->poll(); }
}

}  // namespace

static bool startAutomation(const std::string& path)
{
  if(automation) return false;  // already started
  automation = new MapAutomation;
  automation->start(path);
  if(automation->driver) { return true; }
  delete automation;
  automation = NULL;
  return false;
}

// start automation in a running app (normal mode, not test mode), e.g. from gdb, from any thread and at any
//  stop point: `call (int)ascendStartAutomation("/tmp/a")`, then continue.  Only copies the path and sets a
//  flag (no locks, no allocation), so it can't deadlock w/ other stopped threads; automation starts on the
//  main thread after the next main loop iteration, which needs an event (e.g. move the mouse over the window).
//  Creates the FIFO if needed.  Returns 1 if accepted
static char pendingPath[1024];
static std::atomic<bool> pendingStart{false};

extern "C" int ascendStartAutomation(const char* path)
{
  if(!path || !path[0] || strlen(path) >= sizeof(pendingPath) || pendingStart || automation) { return 0; }
  strcpy(pendingPath, path);
  pendingStart = true;
  return 1;
}

// referenced from the static initializer so the linker keeps ascendStartAutomation (release builds link w/
//  --gc-sections, which removes unreferenced functions; the used attribute doesn't prevent that)
static int (* volatile ascendStartAutomationRef)(const char*) = NULL;

// MapsApp's hook pointers are constant initialized (NULL), so setting them from a static initializer is safe
static struct AutomationInit {
  AutomationInit() {
    ascendStartAutomationRef = ascendStartAutomation;
    MapsApp::startAutomationFn = startAutomation;
    MapsApp::automationPoll = [](bool frameDrawn){
      if(pendingStart) {
        startAutomation(pendingPath);
        pendingStart = false;
      }
      if(automation) { automation->poll(frameDrawn); }
    };
  }
} automationInit;
