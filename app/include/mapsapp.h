#pragma once

#include "tangram.h"
#include <cmath>

#include "mapscomponent.h"
#include "ulib/threadutil.h"

class TouchHandler;
class MapsTracks;
class MapsBookmarks;
class MapsOffline;
class MapsSources;
class MapsSearch;
class PluginManager;

class SvgGui;
class Splitter;
class Pager;
class Widget;
class Button;
class Window;
class Toolbar;
class Menubar;
class SharedMenu;
class MapsWidget;
class ScaleBarWidget;
class CrosshairWidget;
class ProgressCircleWidget;
class ManageColorsDialog;
class SvgNode;
class Color;
class Painter;
class Point;
struct Rect;
struct sqlite3;
struct SDL_Window;
struct Timer;
union SDL_Event;
struct NVGLUframebuffer;
struct NVGSWUblitter;

namespace YAML { class Node; }

class PlatformFile
{
public:
  PlatformFile() {}
  PlatformFile(const PlatformFile& other) = delete;
  virtual ~PlatformFile() {}
  virtual std::string sqliteURI() const = 0;
  virtual std::string fsPath() const = 0;
  virtual std::vector<char> readAll() const = 0;
};

class MapsApp
{
public:
  MapsApp(Platform* _platform);
  ~MapsApp();  // impl must be outside header to allow unique_ptr members w/ incomplete types

  void mapUpdate(double time);
  void onMouseWheel(double x, double y, double scrollx, double scrolly, bool rotating, bool shoving);
  //void onResize(int wWidth, int wHeight, int fWidth, int fHeight);
  void onSuspend();
  void onResume();
  void onLowMemory();
  void onLowPower(int state);
  void updateLocation(const Location& _loc);
  void updateOrientation(double time, float azimuth, float pitch, float roll);
  void updateGpsStatus(int satsVisible, int satsUsed);

  void tapEvent(float x, float y);
  void doubleTapEvent(float x, float y);
  void longPressEvent(float x, float y);
  void hoverEvent(float x, float y);
  void fingerEvent(int action, float x, float y);

  void loadSceneFile(bool async = true, bool setPosition = false);
  bool needsRender() const { return map->getPlatform().isContinuousRendering(); }
  void getMapBounds(LngLat& lngLatMin, LngLat& lngLatMax);
  Rect getMapViewport();
  Point lngLatToScreenPoint(LngLat lngLat);
  LngLat getMapCenter();
  double getElevation(LngLat pos, std::function<void(double)> callback = {});
  typedef std::function<void(int)> PickResultStepper;
  void setPickResult(LngLat pos, std::string namestr, const std::string& propstr, PickResultStepper = {});
  const YAML::Node& sceneConfig();
  void placeInfoPluginError(const char* err);
  int getPanelWidth() const;
  std::string getPlaceTitle(const Properties& props) const;
  void gotoCameraPos(const CameraPosition& campos);
  void updateLocPlaceInfo();
  std::shared_ptr<TileSource> getElevationSource();
  void toggleFollow();

  Location currLocation = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  float orientation = 0;
  float locMarkerAngle = 0;
  CameraPosition prevCamPos;
  enum { NO_FOLLOW = 0, FOLLOW_PENDING, FOLLOW_ACTIVE } followState = NO_FOLLOW;
  double orientationTime = 0;

  MarkerID pickResultMarker = 0;
  MarkerID pickedMarkerId = 0;
  MarkerID locMarker = 0;
  Tangram::MapState mapState;
  std::string pickResultName;
  std::string pickResultProps;
  std::string pickResultOsmId;
  LngLat pickResultCoord = {NAN, NAN};
  PickResultStepper pickResultStepper;
  LngLat tapLocation = {NAN, NAN};
  bool searchActive = false;
  int placeInfoProviderIdx = 0;
  //int gpsSatsUsed = 0;
  bool currLocPlaceInfo = false;  // special case of showing place info for current location
  bool hasLocation = false;
  bool glNeedsInit = true;
  bool drawOnMap = false;
  bool appSuspended = false;
  float topInset = 0, bottomInset = 0;

  std::vector<SceneUpdate> sceneUpdates;
  std::string sceneFile;
  std::string sceneYaml;

  // members constructed in declaration order, destroyed in reverse order, so Map will be destroyed last
  std::unique_ptr<Map> map;
  std::unique_ptr<Painter> painter;
  std::unique_ptr<Window> win;

  std::shared_ptr<ClientDataSource> tracksDataSource;
  std::unique_ptr<TouchHandler> touchHandler;
  std::unique_ptr<MapsTracks> mapsTracks;
  std::unique_ptr<MapsBookmarks> mapsBookmarks;
  std::unique_ptr<MapsOffline> mapsOffline;
  std::unique_ptr<MapsSources> mapsSources;
  std::unique_ptr<MapsSearch> mapsSearch;
  std::unique_ptr<PluginManager> pluginManager;

  // GUI
  void createGUI(SDL_Window* sdlWin);
  void setWindowLayout(int fbWidth, int fbHeight);
  void showPanel(Widget* panel, bool isSubPanel = false);
  void maximizePanel(bool maximize);
  bool popPanel();
  Toolbar* createPanelHeader(const SvgNode* icon, const char* title);
  Button* createPanelButton(const SvgNode* icon, const char* title, Widget* panel, bool menuitem = false);
  Widget* createMapPanel(Toolbar* header, Widget* content, Widget* fixedContent = NULL, bool canMinimize = true);
  void addPlaceInfo(const char* icon, const char* title, const char* value);
  void dumpTileContents(float x, float y);
  bool drawFrame(int fbWidth, int fbHeight);
  void setDpi(float dpi);
  Button* addUndeleteItem(const std::string& title, const SvgNode* icon, std::function<void()> callback);

  Widget* currLayout = NULL;
  Splitter* panelSplitter = NULL;
  Widget* panelSeparator = NULL;
  Widget* panelContainer = NULL;
  Widget* panelContent = NULL;
  Widget* mainTbContainer = NULL;
  Widget* bottomPadding = NULL;
  Widget* infoPanel = NULL;
  Widget* infoContent = NULL;
  Menubar* mainToolbar = NULL;
  Widget* mapsContent = NULL;
  MapsWidget* mapsWidget = NULL;
  Button* reorientBtn = NULL;
  Button* recenterBtn = NULL;
  Widget* gpsStatusBtn = NULL;
  CrosshairWidget* crossHair = NULL;
  Widget* legendContainer = NULL;
  ScaleBarWidget* scaleBar = NULL;
  SharedMenu* colorPickerMenu = NULL;
  std::vector<Widget*> panelHistory;
  std::vector<Widget*> panelPages;
  std::unique_ptr<ManageColorsDialog> customColorDialog;
  Widget* panelToSkip = NULL;
  Menu* undeleteMenu = NULL;
  Pager* panelPager = NULL;
  Button* terrain3dCb = NULL;
  Button* followGPSBtn = NULL;
  ProgressCircleWidget* progressWidget = NULL;
  std::function<bool(SvgGui*, Widget*, SDL_Event*)> pagerEventFilter;

  Timer* progressTimer = NULL;
  float currProgress = 1;

  std::atomic_int_fast64_t storageTotal = {0};
  std::atomic_int_fast64_t storageOffline = {0};

  enum EventTypes { PANEL_CLOSED=0xE001, PANEL_OPENED };

  static bool openURL(const char* url);
  static SvgNode* uiIcon(const char* id);
  static void runOnMainThread(std::function<void()> fn);
  static void messageBox(std::string title, std::string message,
      std::vector<std::string> buttons = {"OK"}, std::function<void(std::string)> callback = {});
  typedef std::function<void(std::unique_ptr<PlatformFile>)> PlatformFileFn_t;
  struct FileDialogFilter_t { const char* name; const char* spec; };
  static void openFileDialog(std::vector<FileDialogFilter_t> filters, PlatformFileFn_t callback);
  typedef std::function<void(const char*)> FilePathFn_t;
  static void pickFolderDialog(FilePathFn_t callback, bool readonly = true);
  static void saveFileDialog(std::vector<FileDialogFilter_t> filters, std::string name, FilePathFn_t callback);
  static void notifyStatusBarBG(bool isLight);
  static void setSensorsEnabled(bool enabled);
  static void setServiceState(int state, float intervalSec = 0, float minDist = 0);
  static void getSafeAreaInsets(float* top, float* bottom);
  static void extractAssets(const char* assetPath);
  static bool loadConfig(const char* assetPath);
  static void sdlEvent(SDL_Event* event);
  static const YAML::Node& cfg() { return config; }  // for use when reading config

  static std::string elevToStr(double meters);
  static std::string distKmToStr(double dist, int prec = 2, int sigdig = 100);

  static MapsApp* inst;
  static Platform* platform;
  static std::string baseDir;
  static SvgGui* gui;
  static YAML::Node config;
  static std::string configFile;
  static sqlite3* bkmkDB;
  static bool metricUnits;
  static bool terrain3D;
  static std::vector<Color> markerColors;
  static ThreadSafeQueue< std::function<void()> > taskQueue;
  static std::thread::id mainThreadId;
  static bool runApplication;
  static bool simulateTouch;
  static bool lowPowerMode;
  static int prevVersion;
  static int versionCode;
  static std::string versionStr;

private:
  void saveConfig();
  void sendMapEvent(MapEvent_t event);
  void showPanelContainer(bool show);
  void clearPickResult();
  void updateLocMarker();

  void populateColorPickerMenu();
  void customizeColors(Color initialColor, std::function<void(Color)> callback);

  bool flyToPickResult = false;
  bool flyingToCurrLoc = false;
  bool initToCurrLoc = false;
  bool sensorsEnabled = true;
  bool panelMaximized = false;
  bool panelMinimized = false;
  bool locMarkerNeedsUpdate = true;
  int shuffleSeed = 0;

  int nvglFBFlags = 0;
  NVGLUframebuffer* nvglFB = NULL;
  NVGSWUblitter* nvglBlit = NULL;
};
