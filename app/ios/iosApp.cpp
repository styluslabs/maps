#include "iosApp.h"
#include "mapsapp.h"

#include "ugui/svggui_platform.h"
#include "ulib/fileutil.h"

namespace Tangram {
Platform* createiOSPlatform();  //#include "iosPlatform.h"  -- Objective-C header
}

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
  return NULL;  //TODO
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
  iosPlatform_showKeyboard(sdlWin, rect);
}

SDL_bool SDL_IsTextInputActive() { return SDL_FALSE; }
void SDL_StartTextInput() {}
void SDL_StopTextInput()
{
  iosPlatform_hideKeyboard(sdlWin);
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
  iosPlatform_setImeText(sdlWin, text, selStart, selEnd);
}

// open file dialog
static MapsApp::OpenFileFn_t openFileCallback;

// filters ignored for now
void MapsApp::openFileDialog(std::vector<FileDialogFilter_t>, OpenFileFn_t callback)
{
  openFileCallback = callback;
  iosPlatform_pickDocument(sdlWin);
}

void MapsApp::pickFolderDialog(OpenFileFn_t callback)
{
  //TODO
}

void MapsApp::saveFileDialog(std::vector<FileDialogFilter_t> filters, std::string name, OpenFileFn_t callback)
{
  if(filters.empty()) return;
  FSPath filePath(MapsApp::baseDir, "temp/" + name + "." + filters.front().spec);
  createPath(filePath.parentPath());
  callback(filePath.c_str());
  if(!filePath.exists()) return;

  iosPlatform_exportDocument(sdlWin, filePath.c_str());
}

bool MapsApp::openURL(const char* url)
{
  iosPlatform_openURL(url);
  return true;
}

void MapsApp::notifyStatusBarBG(bool isLight)
{
  //iosPlatform_setStatusBarBG(sdlWin, isLight);
}

void MapsApp::setSensorsEnabled(bool enabled)
{
  iosPlatform_setSensorsEnabled(sdlWin, enabled);
}

void MapsApp::setServiceState(int state, float intervalSec, float minDist)
{
  //TODO
}

void MapsApp::openBatterySettings() {}

// main loop

int mainLoop(int width, int height, float dpi)
{
  fbWidth = width;  fbHeight = height;
  iosPlatform_setContextCurrent(sdlWin);

  if(!app) {
    app = new MapsApp(MapsApp::platform);
    app->setDpi(dpi);
    app->createGUI(sdlWin);
  }
  else
    MapsApp::mainThreadId = std::this_thread::get_id();
  //app->glNeedsInit = true;
  LOGW("Entering main loop");
  MapsApp::runApplication = true;
  while(MapsApp::runApplication) {
    MapsApp::taskQueue.wait();

    //int fbWidth = 0, fbHeight = 0;
    //SDL_GetWindowSize(&sdlWin, &fbWidth, &fbHeight);
    if(app->drawFrame(fbWidth, fbHeight))
      iosPlatform_swapBuffers(sdlWin);  //display, surface);
    // app not fully initialized until after first frame
    //if(!initialQuery.empty()) {
    //  app->mapsSearch->doSearch(initialQuery);
    //  initialQuery.clear();
    //}
  }
  LOGW("Exiting main loop");
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
  else if(replace || !dest.exists()) {
    LOGW("Copying file: %s to %s", src.c_str(), dest.c_str());
    copyFile(src, dest);
  }
}

void iosApp_startApp(void* glView, const char* bundlePath)
{
  static const int versionCode = 1;
  
  sdlWin = (SDL_Window*)glView;
  const char* ioshome = getenv("HOME");
  MapsApp::baseDir = FSPath(ioshome, "Documents/").path;
  //FSPath iosLibRoot = FSPath(ioshome, "Library/");  -- use Library/Caches to automatically exclude from iCloud?

  bool firstrun = !FSPath(MapsApp::baseDir, "config.default.yaml").exists();
  FSPath assetPath(bundlePath, "assets/");
  if(firstrun)
    copyRecursive(assetPath, MapsApp::baseDir);
  if(MapsApp::loadConfig() && !firstrun)
    copyRecursive(assetPath, MapsApp::baseDir);

  MapsApp::platform = Tangram::createiOSPlatform();
}

void iosApp_startLoop(int width, int height, float dpi)
{
  if(mainThread.joinable()) return;
  mainThread = std::thread(mainLoop, width, height, dpi);
}

void iosApp_stopLoop()
{
  if(mainThread.joinable()) {
    MapsApp::runOnMainThread([=](){ MapsApp::runApplication = false; });
    mainThread.join();
  }
}

void iosApp_imeTextUpdate(const char* text, int selStart, int selEnd)
{
  std::string str(text);
  MapsApp::runOnMainThread([=]() {
    SDL_Event event = {0};
    event.type = SVGGUI_IME_TEXT_UPDATE;
    event.user.data1 = (void*)str.c_str();
    event.user.data2 = (void*)((selEnd << 16) | selStart);
    MapsApp::sdlEvent(&event);
  });
}

void iosApp_onPause()
{
  MapsApp::runOnMainThread([=](){ app->onSuspend(); });
}

void iosApp_onResume()
{
  MapsApp::runOnMainThread([=](){ app->onResume(); });
}

void iosApp_filePicked(const char* path)
{
  std::string s(path);
  if(openFileCallback)
    MapsApp::runOnMainThread([s, cb=std::move(openFileCallback)](){ cb(s.c_str()); });
  openFileCallback = {};
}

void iosApp_updateLocation(double time, double lat, double lng, float poserr,
    double alt, float alterr, float dir, float direrr, float spd, float spderr)
{
  MapsApp::runOnMainThread([=](){
    app->updateLocation(Location{time, lat, lng, poserr, alt, alterr, dir, direrr, spd, spderr});
  });
}

void iosApp_updateOrientation(float azimuth, float pitch, float roll)
{
  MapsApp::runOnMainThread([=](){ app->updateOrientation(azimuth, pitch, roll); });
}
