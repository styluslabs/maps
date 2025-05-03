#include "mapsapp.h"
#include <unistd.h>
#include "ugui/svggui.h"
#include "usvg/svgwriter.h"
#include "../linux/src/linuxPlatform.h"
#include "util/yamlPath.h"
#include "util/elevationManager.h"
#include "util.h"

#include "mapsources.h"
#include "plugins.h"
#include "offlinemaps.h"

//#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_GLEXT
#define GL_GLEXT_PROTOTYPES
#include "ugui/example/glfwSDL.h"
#include "ugui/svggui_util.h"  // sdlEventLog for debugging
#include "stb_image_write.h"


static bool debugHovered = false;
static bool takeScreenshot = false;

void PLATFORM_WakeEventLoop() { glfwPostEmptyEvent(); }
void TANGRAM_WakeEventLoop() { glfwPostEmptyEvent(); }

// only needed for mobile
void PLATFORM_setImeText(const char* text, int selStart, int selEnd) {}

// spent more time trying to get xwd, maim, scrot, etc. working than it took to write this
static void screenshotPng(int width, int height)
{
  Image img(width, height);
  glReadBuffer(GL_FRONT);  // to get MSAA resolve ... in general reading front buffer not guaranteed to work
  // GL_RGB, not GL_RGBA (alpha channel not allowed in screenshots)
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, img.pixels());
  glReadBuffer(GL_BACK);
  auto pngname = std::to_string(mSecSinceEpoch()/1000) + ".png";
  stbi_flip_vertically_on_write(1);
  // initial value of GL_PACK_ALIGNMENT is 4
  stbi_write_png(pngname.c_str(), width, height, 3, img.pixels(), (3*width + 0x3) & ~0x3);
  stbi_flip_vertically_on_write(0);
  //FileStream fstrm(pngname.c_str(), "wb");
  //auto encoded = img.encodePNG();
  //fstrm.write((char*)encoded.data(), encoded.size());
  PLATFORM_LOG("Screenshot written to %s\n", pngname.c_str());
}

void glfwSDLEvent(SDL_Event* event)
{
  event->common.timestamp = SDL_GetTicks();
  if(MapsApp::simulateTouch) {
    if(event->type == SDL_FINGERMOTION && event->tfinger.fingerId == 0)
      return;  // ignore hover events
    if((event->type == SDL_FINGERDOWN || event->type == SDL_FINGERMOTION || event->type == SDL_FINGERUP
        || event->type == SVGGUI_FINGERCANCEL) && event->tfinger.touchId == SDL_TOUCH_MOUSEID) {
      event->tfinger.touchId = 0;
    }
  }
  //LOGW("%s", sdlEventLog(event).c_str());
  if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_PRINTSCREEN) {
    if((event->key.keysym.mod & KMOD_CTRL) && (event->key.keysym.mod & KMOD_SHIFT))
      MapsApp::gui->pushUserEvent(SvgGui::KEYBOARD_HIDDEN, 0);  // can't use a menu item for this obviously
    else if(event->key.keysym.mod & KMOD_CTRL)
      SvgGui::debugDirty = !SvgGui::debugDirty;
    else if(event->key.keysym.mod & KMOD_ALT)
      debugHovered = true;
    else if(event->key.keysym.mod & KMOD_SHIFT) {
      takeScreenshot = true;
      MapsApp::platform->requestRender();
    }
    else
      SvgGui::debugLayout = true;
  }
  else if(std::this_thread::get_id() != MapsApp::mainThreadId)
    MapsApp::runOnMainThread([_event = *event](){ glfwSDLEvent((SDL_Event*)&_event); });
  else
    MapsApp::gui->sdlEvent(event);
}

bool MapsApp::openURL(const char* url)
{
#if PLATFORM_OSX
  return strchr(url, ':') ? macosOpenUrl(url) : macosOpenUrl((std::string("http://") + url).c_str());
#elif IS_DEBUG
  PLATFORM_LOG("openURL: %s\n", url);
  return true;
#else  // Linux
  system(fstring("xdg-open '%s' || x-www-browser '%s' &", url, url).c_str());
  return true;
#endif
}

void MapsApp::notifyStatusBarBG(bool) {}
void MapsApp::setSensorsEnabled(bool enabled) {}
void MapsApp::setServiceState(int state, float intervalSec, float minDist) {}
void MapsApp::getSafeAreaInsets(float *top, float *bottom) { *top = 0; *bottom = 0; }
void MapsApp::extractAssets(const char*) {}

int main(int argc, char* argv[])
{
#if PLATFORM_WIN
  SetProcessDPIAware();
  winLogToConsole = attachParentConsole();  // printing to old console is slow, but Powershell is fine
#endif

  // config
  MapsApp::baseDir = canonicalPath("./");
  if(!FSPath(MapsApp::baseDir, "config.default.yaml").exists()) {
    if(argc > 0)
      MapsApp::baseDir = canonicalPath(FSPath(argv[0]).parentPath());
    if(!FSPath(MapsApp::baseDir, "config.default.yaml").exists())
      MapsApp::baseDir = canonicalPath(FSPath(MapsApp::baseDir, "../../assets/"));
  }
  MapsApp::loadConfig("");

  // command line args
  std::string sceneFile, importFile;  // -f scenes/scene-omt.yaml
  for(int argi = 1; argi < argc-1; argi += 2) {
    YAML::Node* node = NULL;
    if(strcmp(argv[argi], "-f") == 0)
      sceneFile = canonicalPath(argv[argi+1]);
    else if(strcmp(argv[argi], "--import") == 0) {
      importFile = canonicalPath(argv[argi+1]);
      MapsApp::taskQueue.push_back([=](){
        MapsApp::inst->setWindowLayout(1000, 1000);  // required to show panel
        MapsApp::inst->showPanel(MapsApp::inst->mapsOffline->offlinePanel);
        MapsApp::inst->mapsOffline->populateOffline();
        MapsApp::inst->mapsOffline->openForImport(std::make_unique<DesktopFile>(importFile));
      });
    }
    else if(strcmp(argv[argi], "--bbox") == 0) {
      if(argi + 5 >= argc) {
        LOGE("--bbox requires 5 arguments: minlng minlat maxlng maxlat zoom");
        return -1;
      }
      LngLat minLngLat(std::stod(argv[argi+1]), std::stod(argv[argi+2]));
      LngLat maxLngLat(std::stod(argv[argi+3]), std::stod(argv[argi+4]));
      int zoom = std::stoi(argv[argi+5]);
      auto bbox = tileCoveringBounds(minLngLat, maxLngLat, zoom);
      logMsg("Tilemaker bbox for min zoom %d: %.16f,%.16f,%.16f,%.16f\n\n", zoom,
          bbox.first.longitude, bbox.first.latitude, bbox.second.longitude, bbox.second.latitude);
      return 0;
    }
    else if(strncmp(argv[argi], "--", 2) == 0 &&
        (node = Tangram::YamlPath(std::string("+") + (argv[argi] + 2)).get(MapsApp::config))) {
      *node = argv[argi+1];
    }
    else
      LOGE("Unknown command line argument: %s", argv[argi]);
  }

  if(!glfwInit()) { PLATFORM_LOG("glfwInit failed.\n"); return -1; }
#if USE_NVG_GL
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);  //3);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  //glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 1);
#endif
  glfwWindowHint(GLFW_SAMPLES, MapsApp::config["msaa_samples"].as<int>(2));
  glfwWindowHint(GLFW_DEPTH_BITS, 24);
  glfwWindowHint(GLFW_STENCIL_BITS, 8);

  std::string winTitle = "Ascend";
#ifdef MAPS_USE_ASAN
  winTitle += " - ASAN";
#elif defined(MAPS_USE_TSAN)
  winTitle += " - TSAN";
#endif
  GLFWwindow* glfwWin = glfwCreateWindow(1000, 600, winTitle.c_str(), NULL, NULL);
  if(!glfwWin) { PLATFORM_LOG("glfwCreateWindow failed.\n"); return -1; }
  glfwSDLInit(glfwWin);  // setup event callbacks

  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* glfwOffscreen = glfwCreateWindow(100, 100, "Ascend Offscreen", NULL, glfwWin);

  auto offscreenWorker = std::make_unique<Tangram::AsyncWorker>("Ascend offscreen GL worker");
  offscreenWorker->enqueue([=](){ glfwMakeContextCurrent(glfwOffscreen); });
  Tangram::ElevationManager::offscreenWorker = std::move(offscreenWorker);

  glfwMakeContextCurrent(glfwWin);
  glfwSwapInterval(0);  //1); // Enable vsync
  glfwSetTime(0);
  //gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
  const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
  float dpi = std::max(mode->width, mode->height)/11.2f;

  // library for native file dialogs - https://github.com/btzy/nativefiledialog-extended
  // alternatives:
  // - https://github.com/samhocevar/portable-file-dialogs (no GTK)
  // - https://sourceforge.net/projects/tinyfiledialogs (no GTK)
  // - https://github.com/Geequlim/NativeDialogs - last commit 2018
  if(NFD_Init() != NFD_OKAY)
    LOGE("NFD_Init error: %s", NFD_GetError());

  MapsApp* app = new MapsApp(new Tangram::LinuxPlatform());
  app->setDpi(dpi);
  //app->map->setupGL();
  app->createGUI((SDL_Window*)glfwWin);

  app->win->addHandler([&](SvgGui*, SDL_Event* event){
    if(event->type == SDL_KEYDOWN) {
      if(event->key.keysym.sym == SDLK_q && event->key.keysym.mod & KMOD_CTRL)
        MapsApp::runApplication = false;
      else if(event->key.keysym.sym == SDLK_F5) {
        app->mapsSources->reload();
        app->pluginManager->reload();
        app->loadSceneFile();  // reload scene
      }
      else
        return false;
    }
    else if(event->type == SDL_QUIT)
      MapsApp::runApplication = false;
    else
      return false;
    return true;
  });

  app->mapsOffline->resumeDownloads();

  if(!sceneFile.empty()) {
    app->sceneFile = "file://" + sceneFile;
    app->loadSceneFile();
  }
  else if(importFile.empty())
    app->mapsSources->rebuildSource(app->config["sources"]["last_source"].Scalar());

  // Alamo square
  app->updateLocation(Location{0, 37.777, -122.434, 0, 100, 0, NAN, 0, NAN, 0});
  app->updateGpsStatus(10, 10);  // turn location maker blue
  app->updateOrientation(0, M_PI/2, 0, 0);

  while(MapsApp::runApplication) {
    app->needsRender() ? glfwPollEvents() : glfwWaitEvents();

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);  //SDL_GL_GetDrawableSize((SDL_Window*)glfwWin, &fbWidth, &fbHeight);

    if(app->drawFrame(fbWidth, fbHeight))
      glfwSwapBuffers(glfwWin);

    if(takeScreenshot) {
      screenshotPng(fbWidth, fbHeight);
      takeScreenshot = false;
    }
    if(debugHovered && MapsApp::gui->hoveredWidget) {
      MemStream buff;
      SvgWriter::DEBUG_CSS_STYLE = true;
      SvgWriter::save(MapsApp::gui->hoveredWidget->node, buff);
      SvgWriter::DEBUG_CSS_STYLE = false;
      buff.write("", 1);  // write null terminator
      PLATFORM_LOG("%s:\n%s\n", SvgNode::nodePath(MapsApp::gui->hoveredWidget->node).c_str(), buff.data());
      debugHovered = false;
    }
    if(SvgGui::debugLayout) {
      FileStream strm("debug_layout.svg", "wb");
      SvgWriter::DEBUG_CSS_STYLE = true;
      SvgWriter::save(app->win->modalOrSelf()->documentNode(), strm);
      SvgWriter::DEBUG_CSS_STYLE = false;
      PLATFORM_LOG("Post-layout SVG written to debug_layout.svg\n");
      SvgGui::debugLayout = false;
    }
  }

  app->onSuspend();
  delete app;

  offscreenWorker = std::move(Tangram::ElevationManager::offscreenWorker);
  if(offscreenWorker) {
    // GLFW docs say a context must not be current on any other thread for glfwTerminate()
    offscreenWorker->enqueue([=](){ glfwMakeContextCurrent(NULL); });
    offscreenWorker->waitForCompletion();
    offscreenWorker.reset();  // wait for thread exit
  }

  NFD_Quit();
  glfwTerminate();
  return 0;
}
