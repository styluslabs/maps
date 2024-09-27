#include "iosApp.h"
#include "mapsapp.h"

#include "yaml-cpp/yaml.h"
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
static float _topInset = 0;
static float _bottomInset = 0;
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
  return iosPlatform_getClipboardText();
}

SDL_bool SDL_HasClipboardText()
{
  char* text = SDL_GetClipboardText();
  if(text) SDL_free(text);
  return text != NULL ? SDL_TRUE : SDL_FALSE;
}

int SDL_SetClipboardText(const char* text)
{
  iosPlatform_setClipboardText(text);
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
  //LOGW("app -> iOS SEND: '%s', %d,%d ", text, selStart, selEnd);
  iosPlatform_setImeText(sdlWin, text, selStart, selEnd);
}

// open file dialog
static MapsApp::PlatformFileFn_t openFileCallback;

// filters ignored for now
void MapsApp::openFileDialog(std::vector<FileDialogFilter_t>, PlatformFileFn_t callback)
{
  openFileCallback = callback;
  iosPlatform_pickDocument(sdlWin);
}

void MapsApp::pickFolderDialog(FilePathFn_t callback)
{
  // ... not used on iOS (photos are loaded with PhotoKit API)
}

void MapsApp::saveFileDialog(std::vector<FileDialogFilter_t> filters, std::string name, FilePathFn_t callback)
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
  iosPlatform_setStatusBarBG(sdlWin, isLight);
}

void MapsApp::setSensorsEnabled(bool enabled)
{
  iosPlatform_setSensorsEnabled(sdlWin, enabled);
}

void MapsApp::setServiceState(int state, float intervalSec, float minDist)
{
  iosPlatform_setServiceState(sdlWin, state, intervalSec, minDist);
}

void MapsApp::getSafeAreaInsets(float *top, float *bottom)
{
  if(top) *top = _topInset;
  if(bottom) *bottom = _bottomInset;
}

static void copyRecursive(FSPath src, FSPath dest, bool replace = false)
{
  if(isDirectory(src.c_str())) {
    if(!dest.exists())
      createDir(dest.path);
    for(auto& f : lsDirectory(src))
      copyRecursive(src.child(f), dest.child(f), replace);
  }
  else if(replace || !dest.exists()) {
    //LOGW("Copying file: %s to %s", src.c_str(), dest.c_str());
    copyFile(src, dest);
  }
}

void MapsApp::extractAssets(const char* assetPath)
{
  copyRecursive(assetPath, MapsApp::baseDir, true);
}

// main loop

static int mainLoop(int width, int height, float dpi, float topinset, float botinset)
{
  fbWidth = width;  fbHeight = height;  _topInset = topinset;  _bottomInset = botinset;
  iosPlatform_setContextCurrent(sdlWin);

  if(!app) {
    app = new MapsApp(MapsApp::platform);
    app->setDpi(dpi);
    app->createGUI(sdlWin);
    // docs say ~/Library/Caches can be cleared by iOS, so tiles should not be stored there!
    iosPlatform_excludeFromBackup(FSPath(MapsApp::baseDir, "cache/").c_str());
  }
  else
    MapsApp::mainThreadId = std::this_thread::get_id();
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;
  event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
  event.window.data1 = width;
  event.window.data2 = height;
  MapsApp::sdlEvent(&event);
    //app->win->redraw();  // even if window size is unchanged, we want to redraw
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

void iosApp_startApp(void* glView, const char* bundlePath)
{
  sdlWin = (SDL_Window*)glView;
  const char* ioshome = getenv("HOME");
  MapsApp::baseDir = FSPath(ioshome, "Documents/").path;
  //FSPath iosLibRoot = FSPath(ioshome, "Library/");  -- use Library/Caches to automatically exclude from iCloud?
  FSPath assetPath(bundlePath, "assets/");
  MapsApp::loadConfig(assetPath.c_str());

  MapsApp::platform = Tangram::createiOSPlatform();
}

void iosApp_startLoop(int width, int height, float dpi, float topinset, float botinset)
{
  if(mainThread.joinable()) return;
  mainThread = std::thread(mainLoop, width, height, dpi, topinset, botinset);
}

void iosApp_stopLoop()
{
  if(mainThread.joinable()) {
    MapsApp::runOnMainThread([=](){ MapsApp::runApplication = false; });
    mainThread.join();
  }
}

void iosApp_getGLConfig(int* samplesOut)
{
  *samplesOut = MapsApp::config["msaa_samples"].as<int>(2);
}

void iosApp_imeTextUpdate(const char* text, int selStart, int selEnd)
{
  std::string str(text);
  //LOGW("iOS -> app RECV: '%s', %d,%d ", text, selStart, selEnd);
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


class IOSFile : public PlatformFile
{
public:
  std::string mPath;
  std::string securedURL;
  IOSFile(std::string _path, std::string _url = "") : mPath(_path), securedURL(_url) {}
  ~IOSFile() override { if(!securedURL.empty()) iosPlatform_releaseSecuredURL(securedURL.c_str()); }
  std::string fsPath() const override { return mPath; }
  std::string sqliteURI() const override { return "file://" + mPath + "?mode=ro"; }
  std::vector<char> readAll() const override {
    std::vector<char> buff;
    readFile(&buff, mPath.c_str());
    return buff;
  }
};

void iosApp_filePicked(const char* path, const char* url)
{
  auto* file = new IOSFile(path, url);
  if(openFileCallback)
    MapsApp::runOnMainThread([file, cb=std::move(openFileCallback)](){ cb(std::unique_ptr<IOSFile>(file)); });
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
