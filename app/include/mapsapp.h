#pragma once

#include "tangram.h"
#include "util.h"

class MapsTracks;
class MapsBookmarks;
class MapsOffline;
class MapsSources;
class MapsSearch;

class MapsApp
{
public:
  void init(std::unique_ptr<Platform> p, const std::string& _apikey);
  void drawFrame(int w, int h, int display_w, int display_h, double current_time, bool focused);
  void onMouseButton(double time, double x, double y, int button, int action, int mods);
  void onMouseMove(double time, double x, double y, bool pressed);
  void onMouseWheel(double scrollx, double scrolly, bool rotating, bool shoving);
  void onResize(int wWidth, int wHeight, int fWidth, int fHeight);

  void loadSceneFile(bool setPosition = false, std::vector<SceneUpdate> updates = {});
  bool needsRender() const { map->getPlatform().isContinuousRendering(); }
  void getMapBounds(LngLat& lngLatMin, LngLat& lngLatMax);
  bool textureFromSVG(const char* texname, char* svg, float scale = 1.0f);

  MarkerID pickResultMarker = 0;
  MarkerID pickedMarkerId = 0;
  std::string pickResultProps;
  LngLat pickResultCoord = {NAN, NAN};
  std::string pickLabelStr;
  bool searchActive = false;

  std::vector<SceneUpdate> sceneUpdates;
  std::string sceneFile = "scene.yaml";
  std::string sceneYaml;
  bool load_async = true;
  bool show_gui = true;
  bool recreate_context = false;

  float density = 1.0;
  float pixel_scale = 2.0;

  std::unique_ptr<MapsTracks> mapsTracks;
  std::unique_ptr<MapsBookmarks> mapsBookmarks;
  std::unique_ptr<MapsOffline> mapsOffline;
  std::unique_ptr<MapsSources> mapsSources;
  std::unique_ptr<MapsSearch> mapsSearch;

  Map* map;
  static Platform* platform;

private:
  void showGUI();
};

class MapsComponent
{
public:
  MapsComponent(MapsApp* _app) : app(_app) {}
  MapsApp* app;
};
