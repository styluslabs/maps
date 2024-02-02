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
class Widget;
class Button;
class Window;
class Toolbar;
class Menubar;
class MapsWidget;
class ScaleBarWidget;
class SvgNode;
class Color;
class Painter;
struct sqlite3;
struct SDL_Window;
union SDL_Event;

namespace YAML { class Node; }

class MapsApp
{
public:
  MapsApp(Platform* _platform);
  ~MapsApp();  // impl must be outside header to allow unique_ptr members w/ incomplete types

  void mapUpdate(double time);
  void onMouseWheel(double x, double y, double scrollx, double scrolly, bool rotating, bool shoving);
  //void onResize(int wWidth, int wHeight, int fWidth, int fHeight);
  void onSuspend();
  void updateLocation(const Location& _loc);
  void updateOrientation(float azimuth, float pitch, float roll);
  void updateGpsStatus(int satsVisible, int satsUsed);

  void tapEvent(float x, float y);
  void doubleTapEvent(float x, float y);
  void longPressEvent(float x, float y);
  void hoverEvent(float x, float y);

  void loadSceneFile(bool async = true, bool setPosition = false);
  bool needsRender() const { return map->getPlatform().isContinuousRendering(); }
  void getMapBounds(LngLat& lngLatMin, LngLat& lngLatMax);
  LngLat getMapCenter();
  void getElevation(LngLat pos, std::function<void(double)> callback);
  void setPickResult(LngLat pos, std::string namestr, const std::string& propstr);
  YAML::Node readSceneValue(const std::string& yamlPath);
  void placeInfoPluginError(const char* err);
  int getPanelWidth() const;
  std::string getPlaceTitle(const Properties& props) const;

  Location currLocation;
  float orientation = 0;

  MarkerID pickResultMarker = 0;
  MarkerID pickedMarkerId = 0;
  MarkerID locMarker = 0;
  Tangram::MapState mapState;
  std::string pickResultName;
  std::string pickResultProps;
  std::string pickResultOsmId;
  LngLat pickResultCoord = {NAN, NAN};
  LngLat tapLocation = {NAN, NAN};
  bool searchActive = false;
  int placeInfoProviderIdx = 0;
  bool hasLocation = false;
  bool followOrientation = false;
  bool mapMovedManually = false;
  bool glNeedsInit = true;

  std::vector<SceneUpdate> sceneUpdates;
  std::string sceneFile;
  std::string sceneYaml;

  // members constructed in declaration order, destroyed in reverse order, so Map will be destroyed last
  std::unique_ptr<Map> map;
  std::unique_ptr<Painter> painter;
  std::unique_ptr<Painter> scaleBarPainter;
  std::unique_ptr<Window> win;

  std::unique_ptr<TouchHandler> touchHandler;
  std::unique_ptr<MapsTracks> mapsTracks;
  std::unique_ptr<MapsBookmarks> mapsBookmarks;
  std::unique_ptr<MapsOffline> mapsOffline;
  std::unique_ptr<MapsSources> mapsSources;
  std::unique_ptr<MapsSearch> mapsSearch;
  std::unique_ptr<PluginManager> pluginManager;

  // GUI
  void createGUI(SDL_Window* sdlWin);
  void setWindowLayout(int fbWidth);
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

  Widget* currLayout = NULL;
  Splitter* panelSplitter = NULL;
  Widget* panelSeparator = NULL;
  Widget* panelContainer = NULL;
  Widget* panelContent = NULL;
  Widget* mainTbContainer = NULL;
  Widget* infoPanel = NULL;
  Widget* infoContent = NULL;
  Menubar* mainToolbar = NULL;
  Widget* mapsContent = NULL;
  MapsWidget* mapsWidget = NULL;
  Button* reorientBtn = NULL;
  Widget* gpsStatusBtn = NULL;
  Widget* crossHair = NULL;
  Widget* legendContainer = NULL;
  ScaleBarWidget* scaleBar;
  std::vector<Widget*> panelHistory;

  std::atomic_int_fast64_t storageTotal = {0};
  std::atomic_int_fast64_t storageOffline = {0};

  enum EventTypes { PANEL_CLOSED=0xE001, PANEL_OPENED };

  static bool openURL(const char* url);
  static SvgNode* uiIcon(const char* id);
  static void runOnMainThread(std::function<void()> fn);
  static void messageBox(std::string title, std::string message,
      std::vector<std::string> buttons = {"OK"}, std::function<void(std::string)> callback = {});
  typedef std::function<void(const char*)> OpenFileFn_t;
  struct FileDialogFilter_t { const char* name; const char* spec; };
  static void openFileDialog(std::vector<FileDialogFilter_t> filters, OpenFileFn_t callback);
  static void saveFileDialog(std::vector<FileDialogFilter_t> filters, std::string name, OpenFileFn_t callback);
  static void notifyStatusBarBG(bool isLight);
  static void setSensorsEnabled(bool enabled);
  static void sdlEvent(SDL_Event* event);

  static Platform* platform;
  static std::string baseDir;
  static SvgGui* gui;
  static YAML::Node config;
  static std::string configFile;
  static sqlite3* bkmkDB;
  static bool metricUnits;
  static std::vector<Color> markerColors;
  static ThreadSafeQueue< std::function<void()> > taskQueue;
  static std::thread::id mainThreadId;
  static bool runApplication;

private:
  void saveConfig();
  void sendMapEvent(MapEvent_t event);
  void showPanelContainer(bool show);
  void clearPickResult();
  void updateLocMarker();

  bool currLocPlaceInfo = false;  // special case of showing place info for current location
  bool flyToPickResult = false;
  bool sensorsEnabled = true;
};
