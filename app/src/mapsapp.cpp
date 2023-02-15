#include "mapsapp.h"
#include "tangram.h"
#include "scene/scene.h"
#include "style/style.h"  // for making uniforms avail as GUI variables
#include "resources.h"
#include "util.h"
#include "imgui.h"
#include "imgui_stl.h"
#include "glm/common.hpp"
#include "rapidxml/rapidxml.hpp"
#include "rapidjson/document.h"
#include <sys/stat.h>

#include "touchhandler.h"
#include "bookmarks.h"
#include "offlinemaps.h"
#include "tracks.h"
#include "mapsearch.h"
#include "mapsources.h"
#include "plugins.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"

static const char* apiKeyScenePath = "+global.sdk_api_key";

Platform* MapsApp::platform = NULL;
std::string MapsApp::baseDir;
std::string MapsApp::apiKey;
LngLat MapsApp::mapCenter;


void MapsApp::getMapBounds(LngLat& lngLatMin, LngLat& lngLatMax)
{
  int vieww = map->getViewportWidth(), viewh = map->getViewportHeight();
  double lng00, lng01, lng10, lng11, lat00, lat01, lat10, lat11;
  map->screenPositionToLngLat(0, 0, &lng00, &lat00);
  map->screenPositionToLngLat(0, viewh, &lng01, &lat01);
  map->screenPositionToLngLat(vieww, 0, &lng10, &lat10);
  map->screenPositionToLngLat(vieww, viewh, &lng11, &lat11);

  lngLatMin.latitude  = std::min(std::min(lat00, lat01), std::min(lat10, lat11));
  lngLatMin.longitude = std::min(std::min(lng00, lng01), std::min(lng10, lng11));
  lngLatMax.latitude  = std::max(std::max(lat00, lat01), std::max(lat10, lat11));
  lngLatMax.longitude = std::max(std::max(lng00, lng01), std::max(lng10, lng11));
}

void MapsApp::setPickResult(LngLat pos, std::string namestr, const rapidjson::Document& props, int priority)
{
  if(pickResultMarker == 0)
    pickResultMarker = map->markerAdd();
  map->markerSetVisible(pickResultMarker, true);
  // 2nd value is priority (smaller number means higher priority)
  //std::replace(namestr.begin(), namestr.end(), '"', '\'');
  //map->markerSetStylingFromString(pickResultMarker, fstring(searchMarkerStyleStr, "pick-marker-red").c_str());
  map->markerSetStylingFromPath(pickResultMarker, "layers.pick-marker.draw.marker");

  Properties mprops;
  mprops.set("priority", priority);
  mprops.set("name", namestr);
  map->markerSetProperties(pickResultMarker, std::move(mprops));

  map->markerSetPoint(pickResultMarker, pos);
  // ensure marker is visible
  double scrx, scry;
  if(!map->lngLatToScreenPosition(pos.longitude, pos.latitude, &scrx, &scry))
    map->flyTo(CameraPosition{pos.longitude, pos.latitude, 16}, 1.0);  // max(map->getZoom(), 14)

  // show place info panel
  pickResultCoord = pos;
  pickResultProps.CopyFrom(props, pickResultProps.GetAllocator());

  gui->deleteContents(placeInfo, ".listitem");

  resultSplitter->setVisible(true);
  resultPanel->setVisible(true);
  resultListContainer->setVisible(false);
  placeInfoContainer->setVisible(true);

  Widget* item = new Widget(placeInfoProto->clone());

  SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
  if(titlenode)
    titlenode->addText(props.IsObject() && props.HasMember("name") ? props["name"].GetString() : namestr.c_str());

  SvgText* coordnode = static_cast<SvgText*>(item->containerNode()->selectFirst(".lnglat-text"));
  if(coordnode)
    coordnode->addText(fstring("%.6f, %.6f", pos.latitude, pos.longitude).c_str());

  SvgText* distnode = static_cast<SvgText*>(item->containerNode()->selectFirst(".dist-text"));
  double distkm = lngLatDist(mapCenter, pos);
  if(distnode)
    distnode->addText(fstring("%.1f km", distkm).c_str());

  Widget* bkmkSection = mapsBookmarks->getPlaceInfoSection(osmIdFromProps(props), pos);
  if(bkmkSection)
    item->selectFirst(".bkmk-section")->addWidget(bkmkSection);

  //SvgContainerNode* imghost = item->selectFirst(".image-container")->containerNode();
  //imghost->addChild(resultIconNode->clone());

  placeInfo->addWidget(item);
}


void MapsApp::setPickResult(LngLat pos, std::string namestr, std::string propstr, int priority)
{
  rapidjson::Document props;
  props.Parse(propstr.c_str());
  setPickResult(pos, namestr, props, priority);
}

void MapsApp::longPressEvent(float x, float y)
{
  double lng, lat;
  map->screenPositionToLngLat(x, y, &lng, &lat);
  setPickResult(LngLat(lng, lat), "", "");
  mapsSearch->clearSearch();  // ???
}

void MapsApp::doubleTapEvent(float x, float y)
{
  //map->handleDoubleTapGesture(x, y);
  LngLat tapped, current;
  map->screenPositionToLngLat(x, y, &tapped.longitude, &tapped.latitude);
  map->getPosition(current.longitude, current.latitude);
  auto pos = map->getCameraPosition();
  pos.zoom += 1.f;
  pos.longitude = tapped.longitude;
  pos.latitude = tapped.latitude;
  map->setCameraPositionEased(pos, 0.5f, Tangram::EaseType::quint);
}

void MapsApp::tapEvent(float x, float y)
{
  // Single tap recognized
  LngLat location;
  map->screenPositionToLngLat(x, y, &location.longitude, &location.latitude);
  double xx, yy;
  map->lngLatToScreenPosition(location.longitude, location.latitude, &xx, &yy);
  logMsg("tapEvent: %f,%f -> %f,%f (%f, %f)\n", x, y, location.longitude, location.latitude, xx, yy);

  map->pickLabelAt(x, y, [&](const Tangram::LabelPickResult* result) {
    if (!result) {
      logMsg("Pick Label result is null.\n");
      pickLabelStr.clear();
      pickResultProps.clear();
      if(pickResultMarker > 0)
        map->markerSetVisible(pickResultMarker, false);
      pickResultCoord = LngLat(NAN, NAN);
      hidePlaceInfo();
      return;
    }

    std::string itemId = result->touchItem.properties->getAsString("id");
    std::string osmType = result->touchItem.properties->getAsString("osm_type");
    if(itemId.empty())
      itemId = result->touchItem.properties->getAsString("osm_id");
    if(osmType.empty())
      osmType = "node";
    std::string namestr = result->touchItem.properties->getAsString("name");
    setPickResult(result->coordinates, namestr, result->touchItem.properties->toJson());
    mapsSearch->clearSearch();  // ???

    // query OSM API with id - append .json to get JSON instead of XML
    if(!itemId.empty()) {
      auto url = Url("https://www.openstreetmap.org/api/0.6/" + osmType + "/" + itemId);
      map->getPlatform().startUrlRequest(url, [this, url, itemId, osmType](UrlResponse&& response) {
        if(response.error) {
          LOGE("Error fetching %s: %s\n", url.string().c_str(), response.error);
          return;
        }
        response.content.push_back('\0');
        rapidxml::xml_document<> doc;
        doc.parse<0>(response.content.data());
        auto osmNode = doc.first_node("osm");
        auto eleNode = osmNode ? osmNode->first_node(osmType.c_str()) : 0;
        auto tag = eleNode ? eleNode->first_node("tag") : 0;
        if(tag) pickLabelStr += "\n============\nid = " + itemId + "\n";
        while(tag) {
          auto key = tag->first_attribute("k");
          auto val = tag->first_attribute("v");
          pickLabelStr += key->value() + std::string(" = ") + val->value() + std::string("\n");
          tag = tag->next_sibling("tag");
        }
      });
    }
  });

  map->pickMarkerAt(x, y, [&](const Tangram::MarkerPickResult* result) {
    if(!result || result->id == pickResultMarker)
    return;
    // hide pick result marker, since there is already a marker!
    map->markerSetVisible(pickResultMarker, false);
    // looking for search marker or bookmark marker?
    pickedMarkerId = result->id;
  });

  mapsTracks->tapEvent(location);

  map->getPlatform().requestRender();
}

void MapsApp::hoverEvent(float x, float y)
{
  // ???
}

void MapsApp::onMouseButton(double time, double x, double y, int button, int action, int mods)
{
  if(button == 0)
    touchHandler->touchEvent(0, action > 0 ? 1 : -1, time, x*density, y*density, 1.0f);
  else if(action > 0) {
    if(mods == 0x04)  // GLFW_MOD_ALT
      dumpTileContents(x, y);
    else
      longPressEvent(x, y);
  }
}

void MapsApp::onMouseMove(double time, double x, double y, bool pressed)
{
  if(pressed)
    touchHandler->touchEvent(0, 0, time, x*density, y*density, 1.0f);
}

void MapsApp::onMouseWheel(double x, double y, double scrollx, double scrolly, bool rotating, bool shoving)
{
  constexpr double scroll_span_multiplier = 0.05; // scaling for zoom and rotation
  constexpr double scroll_distance_multiplier = 5.0; // scaling for shove

  x *= density;
  y *= density;
  if (shoving) {
    map->handleShoveGesture(scroll_distance_multiplier * scrolly);
  } else if (rotating) {
    map->handleRotateGesture(x, y, scroll_span_multiplier * scrolly);
  } else {
    map->handlePinchGesture(x, y, 1.0 + scroll_span_multiplier * scrolly, 0.f);
  }
}

void MapsApp::loadSceneFile(bool setPosition, std::vector<SceneUpdate> updates)
{
  for (auto& update : sceneUpdates)  // add persistent updates (e.g. API key)
    updates.push_back(update);
  // sceneFile will be used iff sceneYaml is empty
  Tangram::SceneOptions options{sceneYaml, Url(sceneFile), setPosition, updates};
  options.diskTileCacheSize = 256*1024*1024;
  options.diskCacheDir = baseDir + "cache/";
#ifdef DEBUG
  options.debugStyles = true;
#endif
  if(single_tile_worker)
    options.numTileWorkers = 1;  // much easier to debug (alterative is gdb scheduler-locking option)
  map->loadScene(std::move(options), load_async);

  // markers are invalidated ... not sure if we should use SceneReadyCallback for this since map->scene is replaced immediately
  mapsTracks = std::make_unique<MapsTracks>(this);
  mapsSearch = std::make_unique<MapsSearch>(this);
  mapsBookmarks = std::make_unique<MapsBookmarks>(this);
  pickResultMarker = 0;
  locMarker = 0;
}

MapsApp::MapsApp(std::unique_ptr<Platform> p) : touchHandler(new TouchHandler(this))
{
  MapsApp::platform = p.get();
  sceneUpdates.push_back(SceneUpdate(apiKeyScenePath, apiKey));

  // Setup style
  if(show_gui) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    io.FontGlobalScale = 2.0f;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls
  }

  // make sure cache folder exists
  mkdir((baseDir + "cache").c_str(), 0777);

  // Setup tangram
  map = new Tangram::Map(std::move(p));

#ifdef TANGRAM_ANDROID
  mapsSources = std::make_unique<MapsSources>(this, "asset:///mapsources.yaml");
#else
  mapsSources = std::make_unique<MapsSources>(this, "file://" + baseDir + "tangram-es/scenes/mapsources.yaml");
#endif
  mapsOffline = std::make_unique<MapsOffline>(this);
  pluginManager = std::make_unique<PluginManager>(this, baseDir + "plugins");

  //map->setSceneReadyListener([this](SceneID id, const SceneError*) {
  //  std::string svg = fstring(markerSVG, "#CF513D");  // note that SVG parsing is destructive
  //  textureFromSVG("pick-marker-red", (char*)svg.data(), 1.5f);  // slightly bigger
  //});

  map->setPixelScale(pixel_scale);
  // default position: Alamo Square, SF - overriden by scene camera position if async load
  map->setPickRadius(1.0f);
  map->setZoom(15);
  map->setPosition(-122.434668, 37.776444);
}

MapsApp::~MapsApp() { delete map; }

void MapsApp::drawFrame(double time)  //int w, int h, int display_w, int display_h, double current_time, bool focused)
{
  //static bool glReady = false; if(!glReady) { map->setupGL(); glReady = true; }
  static double lastFrameTime = 0;

  //ImGuiIO& io = ImGui::GetIO();
  //LOGW("Rendering frame at %f w/ btn 0 %s at %f,%f", time, io.MouseDown[0] ? "down" : "up", io.MousePos.x, io.MousePos.y);
  if(show_gui) {
    ImGui::NewFrame();
    showImGUI();  // ImGui::ShowDemoWindow();
  }

  //platform->notifyRender();
  Tangram::MapState state = map->update(time - lastFrameTime);
  lastFrameTime = time;
  if (state.isAnimating()) {
    platform->requestRender();
  }

  mapsTracks->onMapChange();
  mapsBookmarks->onMapChange();
  mapsOffline->onMapChange();
  mapsSources->onMapChange();
  mapsSearch->onMapChange();
  pluginManager->onMapChange();

  map->render();

  // update map center
  map->getPosition(mapCenter.longitude, mapCenter.latitude);

  if(show_gui)
    ImGui::Render();
}

void MapsApp::onResize(int wWidth, int wHeight, int fWidth, int fHeight)
{
  float new_density = (float)fWidth / (float)wWidth;
  if (new_density != density) {
    recreate_context = true;
    density = new_density;
  }
  map->setPixelScale(pixel_scale*density);
  map->resize(fWidth, fHeight);
}

void MapsApp::updateLocation(const Location& _loc)
{
  loc = _loc;
  if(!locMarker) {
    locMarker = map->markerAdd();
    map->markerSetStylingFromString(locMarker, locMarkerStyleStr);
    map->markerSetDrawOrder(locMarker, INT_MAX);
  }
  //map->markerSetVisible(locMarker, true);
  map->markerSetPoint(locMarker, loc.lngLat());
}

void MapsApp::updateOrientation(float azimuth, float pitch, float roll)
{
  orientation = azimuth;
}

YAML::Node MapsApp::readSceneValue(const std::string& yamlPath)
{
  YAML::Node node;
  if(map->getScene()->isReady())
      Tangram::YamlPath(yamlPath).get(map->getScene()->config(), node);
  return node;
}

#include <fstream>

void MapsApp::dumpTileContents(float x, float y)
{
  using namespace Tangram;
  static std::mutex logMutex;
  double lng, lat;
  map->screenPositionToLngLat(x, y, &lng, &lat);

  TileTaskCb cb{[](std::shared_ptr<TileTask> task) {
    if(!task->hasData() || !task->source()) return;
    TileID id = task->tileId();
    std::string filename = baseDir + fstring("dump_%s_%d_%d_%d.json", task->source()->name().c_str(), id.z, id.x, id.y);
    std::ofstream fout(filename);
    std::lock_guard<std::mutex> lock(logMutex);
    auto tileData = task->source()->parse(*task);  //task->source() ? : Mvt::parseTile(*task, 0);
    std::vector<std::string> layerstr;
    for(const Layer& layer : tileData->layers) {
      std::vector<std::string> featstr;
      for(const Feature& feature : layer.features)
        featstr.push_back(feature.props.toJson());
      layerstr.push_back("\"" + layer.name + "\": [" + joinStr(featstr, ", ") + "]");
    }
    fout << "{" << joinStr(layerstr, ", ") << "}";
    LOGW("Tile dumped to %s", filename.c_str());
  }};

  auto& tileSources = map->getScene()->tileSources();
  for(auto& src : tileSources) {
    if(!src->isRaster()) {
      TileID tileId = lngLatTile(LngLat(lng, lat), int(map->getZoom()));
      while(tileId.z > src->maxZoom())
        tileId = tileId.getParent();
      auto task = std::make_shared<BinaryTileTask>(tileId, src);
      src->loadTileData(task, cb);
    }
  }
}

void MapsApp::showSceneGUI()
{
    // always show map position ... what's the difference between getPosition/getZoom and getCameraPosition()?
    double lng, lat;
    map->getPosition(lng, lat);
    ImGui::Text("Map: lat,lng,zoom: %.7f, %.7f z%.2f", lat, lng, map->getZoom());
    ImGui::Text("GPS: lat,lng,alt,dir: %.7f, %.7f %.1f m %.0f", loc.lat, loc.lng, loc.alt, orientation);
    if (ImGui::Button("Recenter")) {
      map->flyTo(CameraPosition{loc.lng, loc.lat, map->getZoom()}, 1.0);
    }

    if (ImGui::CollapsingHeader("Scene")) {
        if (ImGui::InputText("Scene URL", &sceneFile, ImGuiInputTextFlags_EnterReturnsTrue)) {
            sceneYaml.clear();
            loadSceneFile();
        }
        if (ImGui::InputText("API key", &apiKey, ImGuiInputTextFlags_EnterReturnsTrue)) {
          if (!apiKey.empty()) {
              sceneUpdates.push_back(SceneUpdate(apiKeyScenePath, apiKey));
          }
          loadSceneFile();  //, {SceneUpdate{apiKeyScenePath, apiKey}});
        }
        if (ImGui::Button("Reload Scene")) {
            loadSceneFile();
        }
    }
}

void MapsApp::showViewportGUI()
{
    if (ImGui::CollapsingHeader("Viewport")) {
        CameraPosition camera = map->getCameraPosition();
        float lngLatZoom[3] = {static_cast<float>(camera.longitude), static_cast<float>(camera.latitude), camera.zoom};
        if (ImGui::InputFloat3("Lng/Lat/Zoom", lngLatZoom, "%.5f", ImGuiInputTextFlags_EnterReturnsTrue)) {
            camera.longitude = lngLatZoom[0];
            camera.latitude = lngLatZoom[1];
            camera.zoom = lngLatZoom[2];
            map->setCameraPosition(camera);
        }
        if (ImGui::SliderAngle("Tilt", &camera.tilt, 0.f, 90.f)) {
            map->setCameraPosition(camera);
        }
        if (ImGui::SliderAngle("Rotation", &camera.rotation, 0.f, 360.f)) {
            map->setCameraPosition(camera);
        }
        Tangram::EdgePadding padding = map->getPadding();
        if (ImGui::InputInt4("Left/Top/Right/Bottom", &padding.left)) {
            map->setPadding(padding);
        }
    }
}

void MapsApp::showDebugFlagsGUI()
{
    using Tangram::DebugFlags;

    if (ImGui::CollapsingHeader("Debug Flags")) {
        bool flag;
        flag = getDebugFlag(DebugFlags::freeze_tiles);
        if (ImGui::Checkbox("Freeze Tiles", &flag)) {
            setDebugFlag(DebugFlags::freeze_tiles, flag);
        }
        flag = getDebugFlag(DebugFlags::proxy_colors);
        if (ImGui::Checkbox("Recolor Proxy Tiles", &flag)) {
            setDebugFlag(DebugFlags::proxy_colors, flag);
        }
        flag = getDebugFlag(DebugFlags::tile_bounds);
        if (ImGui::Checkbox("Show Tile Bounds", &flag)) {
            setDebugFlag(DebugFlags::tile_bounds, flag);
        }
        flag = getDebugFlag(DebugFlags::tile_infos);
        if (ImGui::Checkbox("Show Tile Info", &flag)) {
            setDebugFlag(DebugFlags::tile_infos, flag);
        }
        flag = getDebugFlag(DebugFlags::labels);
        if (ImGui::Checkbox("Show Label Debug Info", &flag)) {
            setDebugFlag(DebugFlags::labels, flag);
        }
        flag = getDebugFlag(DebugFlags::tangram_infos);
        if (ImGui::Checkbox("Show Map Info", &flag)) {
            setDebugFlag(DebugFlags::tangram_infos, flag);
        }
        flag = getDebugFlag(DebugFlags::draw_all_labels);
        if (ImGui::Checkbox("Show All Labels", &flag)) {
            setDebugFlag(DebugFlags::draw_all_labels, flag);
        }
        flag = getDebugFlag(DebugFlags::tangram_stats);
        if (ImGui::Checkbox("Show Frame Stats", &flag)) {
            setDebugFlag(DebugFlags::tangram_stats, flag);
        }
        flag = getDebugFlag(DebugFlags::selection_buffer);
        if (ImGui::Checkbox("Show Selection Buffer", &flag)) {
            setDebugFlag(DebugFlags::selection_buffer, flag);
        }
        ImGui::Checkbox("Wireframe Mode", &wireframe_mode);
        ImGui::Checkbox("Single Tile worker thread", &single_tile_worker);
    }
}

void MapsApp::showSceneVarsGUI()
{
  if (ImGui::CollapsingHeader("Scene Variables", ImGuiTreeNodeFlags_DefaultOpen)) {
    YAML::Node vars = readSceneValue("global.gui_variables");
    for(const auto& var : vars) {
      std::string name = var["name"].as<std::string>("");
      std::string label = var["label"].as<std::string>("");
      std::string reload = var["reload"].as<std::string>("");
      std::string stylename = var["style"].as<std::string>("");
      if(!stylename.empty()) {
        // shader uniform
        auto& styles = map->getScene()->styles();
        for(auto& style : styles) {
          if(style->getName() == stylename) {
            for(auto& uniform : style->styleUniforms()) {
              if(uniform.first.name == name) {
                if(uniform.second.is<float>()) {
                  float val = uniform.second.get<float>();
                  if(ImGui::InputFloat(label.c_str(), &val)) {
                    uniform.second.set<float>(val);
                  }
                }
                else
                  LOGE("Cannot set %s.%s: only float uniforms currently supported in gui_variables!", stylename.c_str(), name.c_str());
                return;
              }
            }
            break;
          }
        }
        LOGE("Cannot find style uniform %s.%s referenced in gui_variables!", stylename.c_str(), name.c_str());
      }
      else {
        // global variable, accessed in scene file by JS functions
        std::string value = readSceneValue("global." + name).as<std::string>("");
        bool flag = value == "true";
        if (ImGui::Checkbox(label.c_str(), &flag)) {
            // we expect only one checkbox to change per frame, so this is OK
            if(reload == "false")  // ... so default to reloading
                map->updateGlobals({SceneUpdate{"global." + name, flag ? "true" : "false"}});
            else
                loadSceneFile(false, {SceneUpdate{"global." + name, flag ? "true" : "false"}});
        }
      }
    }
  }
}

void MapsApp::showPickLabelGUI()
{
    if (ImGui::CollapsingHeader("Picked Object", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted(pickLabelStr.c_str());
    }
}

void MapsApp::showImGUI()
{
  showSceneGUI();
  mapsSources->showGUI();
  mapsOffline->showGUI();
  showViewportGUI();
  mapsTracks->showGUI();
  showDebugFlagsGUI();
  showSceneVarsGUI();
  mapsSearch->showGUI();
  mapsBookmarks->showGUI();
  pluginManager->showGUI();
  showPickLabelGUI();
}

// New GUI

class MapsWidget : public Widget
{
public:
  MapsWidget(MapsApp* _app);

  void draw(SvgPainter* svgp) const override;
  Rect bounds(SvgPainter* svgp) const override;
  Rect dirtyRect() const override;

  MapsApp* app;
  Rect viewport;
};

static int actionFromSDLFinger(unsigned int sdltype)
{
  if(sdltype == SDL_FINGERMOTION) return 0;
  else if(sdltype == SDL_FINGERDOWN) return 1;
  else if(sdltype == SDL_FINGERUP) return -1;
  else if(sdltype == SVGGUI_FINGERCANCEL) return -1;
  return 0;
}

MapsWidget::MapsWidget(MapsApp* _app) : Widget(new SvgCustomNode), app(_app)
{
  onApplyLayout = [this](const Rect& src, const Rect& dest){
    if(dest != viewport) {
      Rect r = dest * (1/app->gui->inputScale);
      real y = window()->winBounds().height()/app->gui->inputScale - r.bottom;
      app->map->setViewport(r.left, y, r.width(), r.height());
    }
    if(src != dest)
      node->invalidate(true);
    viewport = dest;
    return true;
  };

  addHandler([this](SvgGui* gui, SDL_Event* event){
    // dividing by inputScale is a temporary hack - touchHandler should work in device independent coords (and
    //  why doesn't map's pixel scale apply to coords?)
    if(event->type == SDL_FINGERDOWN || event->type == SDL_FINGERUP ||
        (event->type == SDL_FINGERMOTION && event->tfinger.fingerId == SDL_BUTTON_LMASK)) {
      if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK)
        gui->setPressed(this);
      app->touchHandler->touchEvent(0, actionFromSDLFinger(event->type),
          event->tfinger.timestamp/1000.0, event->tfinger.x/gui->inputScale, event->tfinger.y/gui->inputScale, 1.0f);
    }
    else if(event->type == SDL_MOUSEWHEEL) {
      Point p = gui->prevFingerPos;
      uint32_t mods = (PLATFORM_WIN || PLATFORM_LINUX) ? (event->wheel.direction >> 16) : 0;
      app->onMouseWheel(p.x/gui->inputScale, p.y/gui->inputScale, event->wheel.x/120.0, event->wheel.y/120.0, mods & KMOD_ALT, mods & KMOD_CTRL);
    }
    else if(event->type == SvgGui::MULTITOUCH) {
      SDL_Event* fevent = static_cast<SDL_Event*>(event->user.data1);
      auto points = static_cast<std::vector<SDL_Finger>*>(event->user.data2);
      for(const SDL_Finger& pt : *points) {
        int action = pt.id == fevent->tfinger.fingerId ? actionFromSDLFinger(fevent->type) : 0;
        app->touchHandler->touchEvent(0, action, event->user.timestamp/1000.0, pt.x/gui->inputScale, pt.y/gui->inputScale, 1.0f);
      }
    }
    else
      return false;
    return true;
  });
}

Rect MapsWidget::bounds(SvgPainter* svgp) const
{
  return viewport;  //m_layoutTransform.mapRect(scribbleView->screenRect);
}

Rect MapsWidget::dirtyRect() const
{
  return viewport;
  //const Rect& dirty = scribbleView->dirtyRectScreen;
  //return dirty.isValid() ? m_layoutTransform.mapRect(dirty) : Rect();
}

void MapsWidget::draw(SvgPainter* svgp) const
{
  auto t0 = std::chrono::high_resolution_clock::now();
  double currTime = std::chrono::duration<double>(t0.time_since_epoch()).count();
  app->drawFrame(currTime);
}

void MapsApp::hidePlaceInfo()
{
  placeInfoContainer->setVisible(false);
  if(!resultList->containerNode()->children().empty())
    resultListContainer->setVisible(true);
  else {
    resultPanel->setVisible(false);
    resultSplitter->setVisible(false);
  }
}

Window* MapsApp::createGUI()
{
  static const char* mainWindowSVG = R"#(
    <svg class="window" layout="box">
      <g class="window-layout" box-anchor="fill" layout="flex" flex-direction="column">
        <g id="maps-container" box-anchor="fill" layout="box"></g>
        <rect id="results-splitter" class="background splitter" display="none" box-anchor="hfill" width="10" height="10"/>
        <g id="results-container" display="none" box-anchor="hfill" layout="box">
          <rect class="background" box-anchor="fill" x="0" y="0" width="20" height="20" />
          <rect id="results-split-sizer" fill="none" box-anchor="hfill" width="320" height="200"/>

          <g id="list-scroll-container" class="list" display="none" box-anchor="fill" layout="box">
            <svg id="list-scroll-doc" box-anchor="fill">
              <g id="list-scroll-content" box-anchor="hfill" layout="flex" flex-direction="column" flex-wrap="nowrap" justify-content="flex-start">
              </g>
            </svg>
          </g>

          <g id="info-scroll-container" class="list" display="none" box-anchor="fill" layout="flex" flex-direction="column">
            <g id="info-toolbar-container" box-anchor="hfill" layout="box"></g>
            <svg id="info-scroll-doc" box-anchor="fill">
              <g id="info-scroll-content" box-anchor="hfill" layout="flex" flex-direction="column" flex-wrap="nowrap" justify-content="flex-start">
              </g>
            </svg>
          </g>

        </g>
      </g>
    </svg>
  )#";

  static const char* placeInfoProtoSVG = R"#(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g layout="flex" flex-direction="row" box-anchor="left">
        <g class="image-container" margin="2 5"></g>
        <g layout="flex" flex-direction="column" box-anchor="vfill">
          <text class="title-text" margin="0 10"></text>
          <text class="addr-text weak" margin="0 10" font-size="12"></text>
          <text class="lnglat-text weak" margin="0 10" font-size="12"></text>
          <text class="dist-text weak" margin="0 10" font-size="12"></text>
          <g class="bkmk-section" layout="box"></g>
        </g>
      </g>
    </g>
  )#";

  placeInfoProto.reset(loadSVGFragment(placeInfoProtoSVG));

  SvgDocument* winnode = createWindowNode(mainWindowSVG);
  Window* win = new Window(winnode);

  // search box, bookmarks btn, map sources btn, recenter map

  resultSplitter = new Splitter(winnode->selectFirst("#results-splitter"),
      winnode->selectFirst("#results-split-sizer"), Splitter::BOTTOM, 120);
  resultPanel = win->selectFirst("#results-container");

  //resultList = resultPanel->selectFirst("#list-scroll-content");
  SvgDocument* listScrollDoc = static_cast<SvgDocument*>(resultPanel->containerNode()->selectFirst("#list-scroll-doc"));
  resultList = new Widget(listScrollDoc->selectFirst("#list-scroll-content"));
  ScrollWidget* listScroller = new ScrollWidget(listScrollDoc, resultList);

  //placeInfo = resultPanel->selectFirst("#info-scroll-content");
  SvgDocument* infoScrollDoc = static_cast<SvgDocument*>(resultPanel->containerNode()->selectFirst("#info-scroll-doc"));
  placeInfo = new Widget(infoScrollDoc->selectFirst("#info-scroll-content"));
  ScrollWidget* infoScroller = new ScrollWidget(infoScrollDoc, placeInfo);

  resultListContainer = win->selectFirst("#list-scroll-container");
  placeInfoContainer = win->selectFirst("#info-scroll-container");

  auto infoToolbar = createToolbar();
  auto backBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_back.svg"));
  auto minimizeBtn = createToolbutton(SvgGui::useFile(":/icons/chevron_down.svg"));

  backBtn->onClicked = [this](){ hidePlaceInfo(); };

  minimizeBtn->onClicked = [this](){
    resultSplitter->setVisible(false);
    resultPanel->setVisible(false);
  };

  infoToolbar->addWidget(backBtn);
  infoToolbar->addWidget(createStretch());
  infoToolbar->addWidget(minimizeBtn);
  resultPanel->selectFirst("#info-toolbar-container")->addWidget(infoToolbar);

  mapsWidget = new MapsWidget(this);
  mapsWidget->node->setAttribute("box-anchor", "fill");
  mapsWidget->isFocusable = true;

  // toolbar w/ buttons for search, bookmarks, tracks, sources
  Toolbar* mainToolbar = createToolbar();
  Button* tracksBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_select_path.svg"), "Tracks");

  //Button* sourcesBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Sources");
  Widget* searchBtn = mapsSearch->createUI();
  Widget* sourcesBtn = mapsSources->createUI();
  Widget* bkmkBtn = mapsBookmarks->createUI();

  mainToolbar->addWidget(searchBtn);
  mainToolbar->addWidget(bkmkBtn);
  mainToolbar->addWidget(tracksBtn);
  mainToolbar->addWidget(sourcesBtn);


  searchWidget = SearchWidget::create(this);

  Widget* mapsPanel = win->selectFirst("#maps-container");
  mapsPanel->addWidget(mapsWidget);
  mapsPanel->addWidget(searchWidget);

  return win;
}

void MapsApp::showPanel(Widget* panel)
{
  Widget* panelContainer;
  SvgNode* oldpanel = panelContainer->containerNode()->firstChild();
  if(oldpanel)
    panelContainer->containerNode()->removeChild(oldpanel);
  panelContainer->addWidget(panel);
  panelContainer->setVisible(true);
  panelSplitter->setVisible(true);
}

Widget* createHeaderTitle(const SvgNode* icon, const char* title)
{
  if(titlewidget)
    toolbar->addWidget(new Widget(titlewidget));
  if(title)
    toolbar->addWidget(new Widget(createTextNode(title)));
}

Widget* MapsApp::createPanelHeader(std::function<void()> backFn, Widget* titlewidget, bool canMinimize)
{
  auto toolbar = createToolbar();
  auto backBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_back.svg"));
  backBtn->onClicked = backFn ? backFn : [this](){ showPanel(NULL); };

  toolbar->addWidget(backBtn);
  toolbar->addWidget(titlewidget);

  toolbar->addWidget(createStretch());
  //if(panelToolbar)
  //  toolbar->addWidget(panelToolbar);

  if(canMinimize) {
    auto minimizeBtn = createToolbutton(SvgGui::useFile(":/icons/chevron_down.svg"));
    minimizeBtn->onClicked = [this](){ minimizePanel(); };
    toolbar->addWidget(minimizeBtn);
  }

  return toolbar;
}

Widget* MapsApp::createMapPanel(Widget* content, Widget* header, Widget* fixedContent)
{
  auto panel = createColumn();
  panel->addWidget(header);
  if(fixedContent)
    panel->addWidget(fixedContent);
  if(content)
    panel->addWidget(new ScrollWidget(new SvgDocument(), content));
  return panel;
}

#if 1  //PLATFORM_DESKTOP
#include "ugui/svggui_platform.h"
#include "usvg/svgparser.h"
#include "usvg/svgwriter.h"
#include "ulib/platformutil.h"

//#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_GLEXT
#define GL_GLEXT_PROTOTYPES
#include "ugui/example/glfwSDL.h"
#define NANOVG_SW_IMPLEMENTATION
#define NVG_LOG PLATFORM_LOG
#define NVGSWU_GLES2
#include "nanovg-2/src/nanovg_sw.h"
#include "nanovg-2/src/nanovg_sw_utils.h"

#include "../linux/src/linuxPlatform.h"

SvgGui* MapsApp::gui = NULL;

// String resources:
typedef std::unordered_map<std::string, const char*> ResourceMap;
static ResourceMap resourceMap;

static void addStringResources(std::initializer_list<ResourceMap::value_type> values)
{
  resourceMap.insert(values);
}

const char* getResource(const std::string& name)
{
  auto it = resourceMap.find(name);
  return it != resourceMap.end() ? it->second : NULL;
}

// SVG for icons
#define LOAD_RES_FN loadIconRes
#include "scribbleres/res_icons.cpp"

#include "ugui/theme.cpp"

void glfwSDLEvent(SDL_Event* event)
{
  event->common.timestamp = SDL_GetTicks();
  MapsApp::gui->sdlEvent(event);
}

static bool timerEventFired = false;

void PLATFORM_WakeEventLoop()
{
  timerEventFired = true;
  glfwPostEmptyEvent();
}

int main(int argc, char* argv[])
{
  bool runApplication = true;
#if PLATFORM_WIN
  SetProcessDPIAware();
  winLogToConsole = attachParentConsole();  // printing to old console is slow, but Powershell is fine
#endif

  // command line args
  const char* sceneFile = "scenes/scene.yaml";
  for(int argi = 1; argi < argc; ++argi) {
    if(strcmp(argv[argi], "-f") == 0 && ++argi < argc)
      sceneFile = argv[argi];
  }

  Url baseUrl("file:///");
  char pathBuffer[PATH_MAX] = {0};
  if (getcwd(pathBuffer, PATH_MAX) != nullptr) {
      baseUrl = baseUrl.resolve(Url(std::string(pathBuffer) + "/"));
  }
  LOG("Base URL: %s", baseUrl.string().c_str());
  Url sceneUrl = baseUrl.resolve(Url(sceneFile));

  if(!glfwInit()) { PLATFORM_LOG("glfwInit failed.\n"); return -1; }
  /*glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); */
  //glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 1);
  glfwWindowHint(GLFW_SAMPLES, 2);
  glfwWindowHint(GLFW_STENCIL_BITS, 8);

  GLFWwindow* glfwWin = glfwCreateWindow(1000, 600, "Maps (DEBUG)", NULL, NULL);
  if(!glfwWin) { PLATFORM_LOG("glfwCreateWindow failed.\n"); return -1; }
  glfwSDLInit(glfwWin);  // setup event callbacks

  glfwMakeContextCurrent(glfwWin);
  glfwSwapInterval(1); // Enable vsync
  //gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

  int nvgFlags = NVG_AUTOW_DEFAULT | (Painter::sRGB ? NVG_SRGB : 0);
  //int nvglFBFlags = NVG_IMAGE_SRGB;

  NVGcontext* nvgContext = nvgswCreate(nvgFlags);  // | NVG_DEBUG);
  if(!nvgContext) { PLATFORM_LOG("Error creating nanovg context.\n"); return -1; }

  glfwSwapInterval(0);
  glfwSetTime(0);

  Painter::vg = nvgContext;
  Painter::loadFont("sans", "/home/mwhite/maps/tangram-es/scenes/fonts/roboto-regular.ttf");

  SvgParser::openStream = [](const char* name) -> std::istream* {
    if(name[0] == ':' && name[1] == '/') {
      const char* res = getResource(name + 2);
      if(res)
        return new std::istringstream(res);
      name += 2;  //return new std::ifstream(PLATFORM_STR(FSPath(basepath, name+2).c_str()), std::ios_base::in | std::ios_base::binary);
    }
    return new std::ifstream(PLATFORM_STR(name), std::ios_base::in | std::ios_base::binary);
  };

  loadIconRes();
  // hook to support loading from resources; can we move this somewhere to deduplicate w/ other projects?
  setGuiResources(defaultWidgetSVG, defaultStyleCSS);
  SvgGui* gui = new SvgGui();
  MapsApp::gui = gui;  // needed by glfwSDLEvent()
  // scaling
  gui->paintScale = 2.0;  //210.0/150.0;
  gui->inputScale = 1/gui->paintScale;
  nvgAtlasTextThreshold(nvgContext, 24 * gui->paintScale);  // 24px font is default for dialog titles

  Painter* painter = new Painter();
  //NVGLUframebuffer* nvglFB = nvgluCreateFramebuffer(nvgContext, 0, 0, NVGLU_NO_NVG_IMAGE | nvglFBFlags);
  //nvgluSetFramebufferSRGB(1);  // no-op for GLES - sRGB enabled iff FB is sRGB
  NVGSWUblitter* swBlitter = nvgswuCreateBlitter();
  uint32_t* swFB = NULL;

  char* apiKey = getenv("NEXTZEN_API_KEY");
  MapsApp::apiKey = apiKey ? apiKey : "";
  MapsApp::baseDir = "/home/mwhite/maps/";
  MapsApp* app = new MapsApp(std::make_unique<Tangram::LinuxPlatform>());

  app->sceneFile = sceneUrl.string();
  app->map->setupGL();
  app->loadSceneFile(false);

  Window* win = app->createGUI();
  win->sdlWindow = (SDL_Window*)glfwWin;
  win->addHandler([&](SvgGui*, SDL_Event* event){
    if(event->type == SDL_QUIT || (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE))
      runApplication = false;
    else if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_PRINTSCREEN)
      SvgGui::debugLayout = true;
    else if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F12)
      SvgGui::debugDirty = !SvgGui::debugDirty;
    return false;
  });
  gui->showWindow(win, NULL);

  while(runApplication) {
    app->needsRender() ? glfwPollEvents() : glfwWaitEvents();
    if(timerEventFired) {
      timerEventFired = false;
      SDL_Event event = {0};
      event.type = SvgGui::TIMER;
      gui->sdlEvent(&event);
    }

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
    painter->deviceRect = Rect::wh(fbWidth, fbHeight);

    if(MapsApp::platform->notifyRender())
      app->mapsWidget->node->setDirty(SvgNode::PIXELS_DIRTY);

    Rect dirty = gui->layoutAndDraw(painter);
    if(SvgGui::debugLayout) {
      XmlStreamWriter xmlwriter;
      SvgWriter::DEBUG_CSS_STYLE = true;
      SvgWriter(xmlwriter).serialize(win->modalOrSelf()->documentNode());
      SvgWriter::DEBUG_CSS_STYLE = false;
      xmlwriter.saveFile("debug_layout.svg");
      PLATFORM_LOG("Post-layout SVG written to debug_layout.svg\n");
      SvgGui::debugLayout = false;
    }
    if(!dirty.isValid())
      continue;

    bool sizeChanged = swFB && (fbWidth != swBlitter->width || fbHeight != swBlitter->height);
    if(!swFB || sizeChanged)
      swFB = (uint32_t*)realloc(swFB, fbWidth*fbHeight*4);
    nvgswSetFramebuffer(Painter::vg, swFB, fbWidth, fbHeight, 0, 8, 16, 24);

    // clear dirty rect to transparent pixels
    if(SvgGui::debugDirty)
      memset(swFB, 0, fbWidth*fbHeight*4);
    else {
      for(int yy = dirty.top; yy < dirty.bottom; ++yy)
        memset(&swFB[int(dirty.left) + yy*fbWidth], 0, 4*size_t(dirty.width()));
    }

    dirty = Rect::wh(fbWidth, fbHeight);
    //painter->fillRect(Rect::wh(fbWidth, fbHeight), Color::RED);
    painter->endFrame();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    nvgswuBlit(swBlitter, swFB, fbWidth, fbHeight,
        int(dirty.left), int(dirty.top), int(dirty.width()), int(dirty.height()));

    glfwSwapBuffers(glfwWin);
  }

  gui->closeWindow(win);
  delete gui;
  delete painter;
  delete app;
  nvgswuDeleteBlitter(swBlitter);
  nvgswDelete(nvgContext);
  glfwTerminate();
  return 0;
}
#endif

// rasterizing SVG markers

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

namespace Tangram {

bool userLoadSvg(char* svg, Texture* texture)  //float scale, int& w, int& h)
{
  NSVGimage* image = nsvgParse(svg, "px", 96.0f);  // nsvgParse is destructive
  if (!image) return false;
  nsvgApplyViewXform(image);

  auto atlas = std::make_unique<SpriteAtlas>();
  bool hasSprites = false;
  NSVGshape* shape = image->shapes;
  while(shape) {
    if(shape->id[0] && !shape->flags) {
      hasSprites = true;
      glm::vec2 pos(shape->bounds[0], shape->bounds[1]);
      glm::vec2 size(shape->bounds[2] - shape->bounds[0], shape->bounds[3] - shape->bounds[1]);
      atlas->addSpriteNode(shape->id, pos, size);
    }
    shape = shape->next;
  }
  if(hasSprites)
    texture->setSpriteAtlas(std::move(atlas));

  float scale = 1.0;
  int w = int(image->width*scale + 0.5f);
  int h = int(image->height*scale + 0.5f);
  NSVGrasterizer* rast = nsvgCreateRasterizer();
  if (!rast) return false;  // OOM, so we don't care about NSVGimage leak
  std::vector<uint8_t> img(w*h*4, 0);
  // note the hack to flip y-axis - should be moved into nanosvgrast.h, activated w/ h < 0
  nsvgRasterize(rast, image, 0, 0, scale, &img[w*(h-1)*4], w, h, -w*4);
  nsvgDelete(image);
  nsvgDeleteRasterizer(rast);
  texture->setPixelData(w, h, 4, img.data(), img.size());
  return true;
}

}

// create image w/ dimensions w,h from SVG string svg and upload to scene as texture with name texname
// since SVG can now be loaded from scene file, not sure we'll need this fn
/*bool MapsApp::textureFromSVG(const char* texname, char* svg, float scale)
{
  //image = nsvgParseFromFile(filename, "px", dpi);
  NSVGimage* image = nsvgParse(svg, "px", 96.0f);  // nsvgParse is destructive
  if (!image) return false;
  scale *= pixel_scale;
  int w = int(image->width*scale + 0.5f), h = int(image->height*scale + 0.5f);
  NSVGrasterizer* rast = nsvgCreateRasterizer();
  if (!rast) return false;  // OOM, so we don't care about NSVGimage leak
  std::vector<uint8_t> img(w*h*4, 0);
  // note the hack to flip y-axis - should be moved into nanosvgrast.h, activated w/ h < 0
  nsvgRasterize(rast, image, 0,0,scale, &img[w*(h-1)*4], w, h, -w*4);
  nsvgDelete(image);
  nsvgDeleteRasterizer(rast);
  //int w, h;
  //auto img = userLoadSvg(svg, scale*pixel_scale, w, h);
  TextureOptions texoptions;
  texoptions.displayScale = 1/pixel_scale;
  map->getScene()->sceneTextures().add(texname, w, h, img.data(), texoptions);
  return true;
}*/
