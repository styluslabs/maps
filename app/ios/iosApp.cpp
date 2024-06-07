#include "mapsapp.h"
//#include "iosPlatform.h"  -- Objective-C header

#include "ugui/svggui_platform.h"
#include "ulib/fileutil.h"

extern "C" {
  void OpenGLView_setContextCurrent(void* _view);
  void OpenGLView_swapBuffers(void* _view);
  Platform* createiOSPlatform();

  void iosApp_startLoop(void* glView, int width, int height, float dpi);
  void iosApp_stopLoop();
  void iosApp_startApp();
};

static MapsApp* app = NULL;
static std::thread mainThread;
static SDL_Window* sdlWin = NULL;
static int fbWidth = 0;
static int fbHeight = 0;
static std::string initialQuery;

// since event loop waits on MapsApp::taskQueue, no need for PLATFORM_WakeEventLoop
void PLATFORM_WakeEventLoop() {}
// note we have to push task even if on main thread so we don't get stuck waiting for external event!
void TANGRAM_WakeEventLoop() { MapsApp::taskQueue.push_back([](){}); }

Uint32 SDL_GetTicks()
{
  static Timestamp t0 = mSecSinceEpoch();
  return mSecSinceEpoch() - t0;
}

char* SDL_GetClipboardText()
{
  return NULL;
}

SDL_bool SDL_HasClipboardText()
{
  char* text = SDL_GetClipboardText();
  if(text) SDL_free(text);
  return text != NULL ? SDL_TRUE : SDL_FALSE;
}

int SDL_SetClipboardText(const char* text)
{
  return 0;
}

void SDL_free(void* mem) { free(mem); }

SDL_Keymod SDL_GetModState()
{
  return (SDL_Keymod)(0);
}

void SDL_SetTextInputRect(SDL_Rect* rect)
{
  //TODO
}

SDL_bool SDL_IsTextInputActive() { return SDL_FALSE; }
void SDL_StartTextInput() {}
void SDL_StopTextInput()
{
  //TODO
}

void SDL_RaiseWindow(SDL_Window* window) {}
void SDL_SetWindowTitle(SDL_Window* win, const char* title) {}
void SDL_GetWindowSize(SDL_Window* win, int* w, int* h) //{ SDL_GL_GetDrawableSize(win, w, h); }
{
  //if(!win) { *w = 1000; *h = 1000; return; }
  *w = fbWidth;  *h = fbHeight;  //OpenGLView_getSize(win, w, h);
}
void SDL_GetWindowPosition(SDL_Window* win, int* x, int* y) { *x = 0; *y = 0; }
void SDL_DestroyWindow(SDL_Window* win) {}
SDL_Window* SDL_GetWindowFromID(Uint32 id) { return NULL; }

int SDL_PushEvent(SDL_Event* event)
{
  MapsApp::sdlEvent(event);
  return 1;
}

int SDL_PeepEvents(SDL_Event* events, int numevents, SDL_eventaction action, Uint32 minType, Uint32 maxType)
{
  if(action != SDL_ADDEVENT) return 0;
  for(int ii = 0; ii < numevents; ++ii)
    MapsApp::sdlEvent(&events[ii]);
  return numevents;
}

void PLATFORM_setImeText(const char* text, int selStart, int selEnd)
{
  //TODO
}

// open file dialog
static MapsApp::OpenFileFn_t openFileCallback;

// filters ignored for now
void MapsApp::openFileDialog(std::vector<FileDialogFilter_t>, OpenFileFn_t callback)
{
  //TODO
}

void MapsApp::pickFolderDialog(OpenFileFn_t callback)
{
  //TODO
}

void MapsApp::saveFileDialog(std::vector<FileDialogFilter_t> filters, std::string name, OpenFileFn_t callback)
{
  //TODO
}

bool MapsApp::openURL(const char* url)
{
  //TODO
  return true;
}

void MapsApp::notifyStatusBarBG(bool isLight)
{
  //TODO
}

void MapsApp::setSensorsEnabled(bool enabled)
{
  //TODO
}

void MapsApp::setServiceState(int state, float intervalSec, float minDist)
{
  //TODO
}

void MapsApp::openBatterySettings()
{
  //TODO
}

// main loop

int mainLoop(void* glView, int width, int height, float dpi)
{
  sdlWin = (SDL_Window*)glView;  fbWidth = width;  fbHeight = height;
  OpenGLView_setContextCurrent(glView);
  
  if(!app) {
    app = new MapsApp(MapsApp::platform);
    app->createGUI(sdlWin);
  }
  app->setDpi(dpi);
  MapsApp::runApplication = true;
  while(MapsApp::runApplication) {
    MapsApp::taskQueue.wait();

    //int fbWidth = 0, fbHeight = 0;
    //SDL_GetWindowSize(&sdlWin, &fbWidth, &fbHeight);
    if(app->drawFrame(fbWidth, fbHeight))
      OpenGLView_swapBuffers(glView);  //display, surface);
    // app not fully initialized until after first frame
    //if(!initialQuery.empty()) {
    //  app->mapsSearch->doSearch(initialQuery);
    //  initialQuery.clear();
    //}
  }
  sdlWin = NULL;
  return 0;
}

void copyRecursive(FSPath src, FSPath dest, bool replace = false)
{
  if(isDirectory(src.c_str())) {
    if(!dest.exists())
      createDir(dest.path);
    for(auto& f : lsDirectory(src))
      copyRecursive(src.child(f), dest.child(f), replace);
  }
  else if(replace || !dest.exists())
    copyFile(src, dest);
}

void iosApp_startApp()
{
  static const int versionCode = 1;
  
  const char* ioshome = getenv("HOME");
  MapsApp::baseDir = FSPath(ioshome, "Documents/").path;
  //FSPath iosLibRoot = FSPath(ioshome, "Library/");  -- use Library/Caches to automatically exclude from iCloud?

  bool firstrun = !FSPath(MapsApp::baseDir, "config.default.yaml").exists();
  if(firstrun)
    copyRecursive("./assets", MapsApp::baseDir);
  if(MapsApp::loadConfig() && !firstrun)
    copyRecursive("./assets", MapsApp::baseDir);
  
  MapsApp::platform = createiOSPlatform();
}

void iosApp_startLoop(void* glView, int width, int height, float dpi)
{
  if(mainThread.joinable()) return;
  mainThread = std::thread(mainLoop, glView, width, height, dpi);
}

void iosApp_stopLoop()
{
  if(mainThread.joinable()) {
    MapsApp::runOnMainThread([=](){ MapsApp::runApplication = false; });
    mainThread.join();
  }
}
