#include "mapsapp.h"
#include <unistd.h>
#include "ugui/svggui.h"
#include "usvg/svgwriter.h"
#include "../linux/src/linuxPlatform.h"
#include "util/yamlPath.h"
#include "nfd.h"

#include "mapsources.h"
#include "plugins.h"
#include "offlinemaps.h"

//#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_GLEXT
#define GL_GLEXT_PROTOTYPES
#include "ugui/example/glfwSDL.h"
#include "ugui/svggui_util.h"  // sdlEventLog for debugging


void PLATFORM_WakeEventLoop() { glfwPostEmptyEvent(); }
void TANGRAM_WakeEventLoop() { glfwPostEmptyEvent(); }

// only needed for mobile
void PLATFORM_setImeText(const char* text, int selStart, int selEnd) {}

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
  if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_PRINTSCREEN)
    SvgGui::debugLayout = true;
  else if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F12)
    SvgGui::debugDirty = !SvgGui::debugDirty;
  else if(std::this_thread::get_id() != MapsApp::mainThreadId)
    MapsApp::runOnMainThread([_event = *event](){ glfwSDLEvent((SDL_Event*)&_event); });
  else
    MapsApp::gui->sdlEvent(event);
}

void MapsApp::openFileDialog(std::vector<FileDialogFilter_t> filters, OpenFileFn_t callback)
{
  nfdchar_t* outPath;
  nfdresult_t result = NFD_OpenDialog(&outPath, (nfdfilteritem_t*)filters.data(), filters.size(), NULL);
  if(result == NFD_OKAY) {
    callback(outPath);
    NFD_FreePath(outPath);
  }
}

void MapsApp::pickFolderDialog(OpenFileFn_t callback)
{
  nfdchar_t* outPath;
  nfdresult_t result = NFD_PickFolder(&outPath, NULL);
  if(result == NFD_OKAY) {
    callback(outPath);
    NFD_FreePath(outPath);
  }
}

void MapsApp::saveFileDialog(std::vector<FileDialogFilter_t> filters, std::string name, OpenFileFn_t callback)
{
  ASSERT(!filters.empty() && "saveFileDialog requires filters!");  // filters required for Android, so test here
  nfdchar_t* outPath;
  if(!filters.empty())
    name.append(".").append(filters.back().spec);
  nfdresult_t result = NFD_SaveDialog(&outPath, (nfdfilteritem_t*)filters.data(), filters.size(), NULL, name.c_str());
  if(result == NFD_OKAY) {
    callback(outPath);
    NFD_FreePath(outPath);
  }
}

void MapsApp::notifyStatusBarBG(bool) {}
void MapsApp::setSensorsEnabled(bool enabled) {}

int main(int argc, char* argv[])
{
#if PLATFORM_WIN
  SetProcessDPIAware();
  winLogToConsole = attachParentConsole();  // printing to old console is slow, but Powershell is fine
#endif

  // config
  MapsApp::baseDir = canonicalPath("./");  //"/home/mwhite/maps/";  //argc > 0 ? FSPath(argv[0]).parentPath() : ".";
  FSPath configPath(MapsApp::baseDir, "config.yaml");
  MapsApp::configFile = configPath.c_str();
  MapsApp::config = YAML::LoadFile(configPath.exists() ? configPath.path
      : configPath.parent().childPath(configPath.baseName() + ".default.yaml"));

  // command line args
  const char* sceneFile = NULL;  // -f scenes/scene-omt.yaml
  for(int argi = 1; argi < argc-1; argi += 2) {
    YAML::Node node;
    if(strcmp(argv[argi], "-f") == 0)
      sceneFile = argv[argi+1];
    else if(strncmp(argv[argi], "--", 2) == 0 && Tangram::YamlPath(std::string("+") + (argv[argi] + 2)).get(MapsApp::config, node))
      node = argv[argi+1];
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
  glfwWindowHint(GLFW_STENCIL_BITS, 8);

  GLFWwindow* glfwWin = glfwCreateWindow(1000, 600, "Maps (DEBUG)", NULL, NULL);
  if(!glfwWin) { PLATFORM_LOG("glfwCreateWindow failed.\n"); return -1; }
  glfwSDLInit(glfwWin);  // setup event callbacks

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
  NFD_Init();

  MapsApp* app = new MapsApp(new Tangram::LinuxPlatform());
  app->setDpi(dpi);
  app->map->setupGL();
  app->createGUI((SDL_Window*)glfwWin);

  app->win->addHandler([&](SvgGui*, SDL_Event* event){
    if(event->type == SDL_QUIT || (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE))
      MapsApp::runApplication = false;
    else if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F5) {
      app->pluginManager->reload(MapsApp::baseDir + "plugins");
      app->loadSceneFile();  // reload scene
      app->mapsSources->sceneVarsLoaded = false;
    }
    return false;
  });

  app->mapsOffline->resumeDownloads();

  if(sceneFile) {
    Url baseUrl("file:///");
    char pathBuffer[256] = {0};
    if (getcwd(pathBuffer, 256) != nullptr) {
        baseUrl = baseUrl.resolve(Url(std::string(pathBuffer) + "/"));
    }
    app->sceneFile = baseUrl.resolve(Url(sceneFile)).string();
    app->loadSceneFile();
  }
  else
    app->mapsSources->rebuildSource(app->config["sources"]["last_source"].Scalar());

  // Alamo square
  app->updateLocation(Location{0, 37.777, -122.434, 0, 100, 0, 0, 0, 0, 0});
  app->updateGpsStatus(10,10);  // turn location maker blue
  app->updateOrientation(M_PI/2, 0, 0);

  while(MapsApp::runApplication) {
    app->needsRender() ? glfwPollEvents() : glfwWaitEvents();

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);  //SDL_GL_GetDrawableSize((SDL_Window*)glfwWin, &fbWidth, &fbHeight);

    if(app->drawFrame(fbWidth, fbHeight))
      glfwSwapBuffers(glfwWin);

    if(SvgGui::debugLayout) {
      XmlStreamWriter xmlwriter;
      SvgWriter::DEBUG_CSS_STYLE = true;
      SvgWriter(xmlwriter).serialize(app->win->modalOrSelf()->documentNode());
      SvgWriter::DEBUG_CSS_STYLE = false;
      xmlwriter.saveFile("debug_layout.svg");
      PLATFORM_LOG("Post-layout SVG written to debug_layout.svg\n");
      SvgGui::debugLayout = false;
    }
  }

  app->onSuspend();
  delete app;

  NFD_Quit();
  glfwTerminate();
  return 0;
}
