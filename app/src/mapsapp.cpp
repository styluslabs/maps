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

#include "bookmarks.h"
#include "offlinemaps.h"
#include "tracks.h"
#include "mapsearch.h"
#include "mapsources.h"

#include "imgui_impl_generic.h"  // for GLFW constants

using namespace Tangram;

constexpr double double_tap_time = 0.5; // seconds
constexpr double scroll_span_multiplier = 0.05; // scaling for zoom and rotation
constexpr double scroll_distance_multiplier = 5.0; // scaling for shove
constexpr double single_tap_time = 0.25; //seconds (to avoid a long press being considered as a tap)

static bool was_panning = false;
static double last_time_released = -double_tap_time; // First click should never trigger a double tap
static double last_time_pressed = 0.0;
static double last_time_moved = 0.0;
static double last_x_down = 0.0;
static double last_y_down = 0.0;
static double last_x_velocity = 0.0;
static double last_y_velocity = 0.0;
static double last_x = 0;
static double last_y = 0;

static const char* apiKeyScenePath = "+global.sdk_api_key";

Platform* MapsApp::platform = NULL;
std::string MapsApp::baseDir;
std::string MapsApp::apiKey;


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

void MapsApp::onMouseButton(double time, double x, double y, int button, int action, int mods)
{
  last_x = x;
  last_y = y;
#if PLATFORM_MOBILE
  ImGui_ImplGeneric_MouseButtonCallback(button, action, mods);
#endif
  if (ImGui::GetIO().WantCaptureMouse) {
    map->getPlatform().requestRender();  // necessary for proper update of combo boxes, etc
    return; // Imgui is handling this event.
  }

  x *= density;
  y *= density;

  if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    // drop a pin at location
    map->screenPositionToLngLat(x, y, &pickResultCoord.longitude, &pickResultCoord.latitude);
    if (pickResultMarker == 0) {
      pickResultMarker = map->markerAdd();
    }
    map->markerSetStylingFromPath(pickResultMarker, "layers.pick-result.draw.pick-marker");
    map->markerSetPoint(pickResultMarker, pickResultCoord);
    map->markerSetVisible(pickResultMarker, true);
    mapsSearch->clearSearch();  // ???
    pickResultProps.clear();
    pickLabelStr = fstring("lat = %.6f\nlon = %.6f", pickResultCoord.latitude, pickResultCoord.longitude);
    return;
  }
  else if (button != GLFW_MOUSE_BUTTON_LEFT) {
    return; // This event is for a mouse button that we don't care about
  }

  if (was_panning && action == GLFW_RELEASE) {
    was_panning = false;
    auto vx = glm::clamp(last_x_velocity, -2000.0, 2000.0);
    auto vy = glm::clamp(last_y_velocity, -2000.0, 2000.0);
    map->handleFlingGesture(x, y, vx, vy);
    return; // Clicks with movement don't count as taps, so stop here
  }

  if (action == GLFW_PRESS) {
    map->handlePanGesture(0.0f, 0.0f, 0.0f, 0.0f);
    last_x_down = x;
    last_y_down = y;
    last_time_pressed = time;
    return;
  }

  if ((time - last_time_released) < double_tap_time) {
    // Double tap recognized
    const float duration = 0.5f;
    Tangram::LngLat tapped, current;
    map->screenPositionToLngLat(x, y, &tapped.longitude, &tapped.latitude);
    map->getPosition(current.longitude, current.latitude);
    auto pos = map->getCameraPosition();
    pos.zoom += 1.f;
    pos.longitude = tapped.longitude;
    pos.latitude = tapped.latitude;

    map->setCameraPositionEased(pos, duration, EaseType::quint);
  } else if ((time - last_time_pressed) < single_tap_time) {
    // Single tap recognized
    LngLat location;
    map->screenPositionToLngLat(x, y, &location.longitude, &location.latitude);
    double xx, yy;
    map->lngLatToScreenPosition(location.longitude, location.latitude, &xx, &yy);

    logMsg("------\n");
    logMsg("LngLat: %f, %f\n", location.longitude, location.latitude);
    logMsg("Clicked:  %f, %f\n", x, y);
    logMsg("Remapped: %f, %f\n", xx, yy);

    map->pickLabelAt(x, y, [&](const LabelPickResult* result) {
      pickLabelStr.clear();
      if (pickResultMarker == 0) {
        pickResultMarker = map->markerAdd();
      }
      if (!result) {
        logMsg("Pick Label result is null.\n");
        map->markerSetVisible(pickResultMarker, false);
        pickResultCoord = LngLat(NAN, NAN);
        return;
      }

      std::string itemId;
      std::string namestr;
      logMsg("Pick label result:\n");
      for (const auto& item : result->touchItem.properties->items()) {
        if(item.key == "id")
          itemId = Properties::asString(item.value);
        else if(item.key == "name")
          namestr = Properties::asString(item.value);
        std::string l = "  " + item.key + " = " + Properties::asString(item.value) + "\n";
        logMsg(l.c_str());
        pickLabelStr += l;
      }
      // save for use when creating bookmark
      pickResultProps = result->touchItem.properties->toJson();
      pickResultCoord = result->coordinates;

      map->markerSetStylingFromString(pickResultMarker,
        fstring(searchMarkerStyleStr, "marker-stroked", 2, namestr.c_str()).c_str());
      map->markerSetPoint(pickResultMarker, result->coordinates);
      map->markerSetVisible(pickResultMarker, true);
      mapsSearch->clearSearch();  // ???

      // query OSM API with id - append .json to get JSON instead of XML
      if(!itemId.empty()) {
        auto url = Url("https://www.openstreetmap.org/api/0.6/node/" + itemId);
        map->getPlatform().startUrlRequest(url, [this, url, itemId](UrlResponse&& response) {
        if(response.error) {
          logMsg("Error fetching %s: %s\n", url.data().c_str(), response.error);
          return;
        }
        response.content.push_back('\0');
        rapidxml::xml_document<> doc;
        doc.parse<0>(response.content.data());
        auto tag = doc.first_node("osm")->first_node("node")->first_node("tag");
        if(tag) pickLabelStr = "id = " + itemId + "\n";
        while(tag) {
          auto key = tag->first_attribute("k");
          auto val = tag->first_attribute("v");
          pickLabelStr += key->value() + std::string(" = ") + val->value() + std::string("\n");
          tag = tag->next_sibling("tag");
        }
        });
      }
    });

    map->pickMarkerAt(x, y, [&](const MarkerPickResult* result) {
      if(!result || result->id == pickResultMarker)
      return;
      // hide pick result marker, since there is already a marker!
      map->markerSetVisible(pickResultMarker, false);
      // looking for search marker or bookmark marker?
      pickedMarkerId = result->id;
    });

    mapsTracks->onClick(location);

    map->getPlatform().requestRender();
  }

  last_time_released = time;
}

void MapsApp::onMouseMove(double time, double x, double y, bool pressed)
{
  last_x = x;
  last_y = y;

  if (ImGui::GetIO().WantCaptureMouse) {
    return; // Imgui is handling this event.
  }

  x *= density;
  y *= density;
  if (pressed) {
    if (was_panning) {
      map->handlePanGesture(last_x_down, last_y_down, x, y);
    }
    was_panning = true;
    last_x_velocity = (x - last_x_down) / (time - last_time_moved);
    last_y_velocity = (y - last_y_down) / (time - last_time_moved);
    last_x_down = x;
    last_y_down = y;
  }
  last_time_moved = time;
}


void MapsApp::onMouseWheel(double scrollx, double scrolly, bool rotating, bool shoving)
{
#if PLATFORM_MOBILE
  ImGui_ImplGeneric_ScrollCallback(scrollx, scrolly);
#endif
  if (ImGui::GetIO().WantCaptureMouse) {
    return; // Imgui is handling this event.
  }

  double x = last_x * density;
  double y = last_y * density;
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
    SceneOptions options{sceneYaml, Url(sceneFile), setPosition, updates};
    options.diskTileCacheSize = 256*1024*1024;
    options.diskCacheDir = baseDir + "cache/";
#ifdef DEBUG
    options.debugStyles = true;
#endif
    map->loadScene(std::move(options), load_async);

    // markers are invalidated ... technically we should use SceneReadyCallback for this if loading async
    mapsTracks = std::make_unique<MapsTracks>(this);
    mapsSearch = std::make_unique<MapsSearch>(this);
    mapsBookmarks = std::make_unique<MapsBookmarks>(this);
    pickResultMarker = 0;
}

MapsApp::MapsApp(std::unique_ptr<Platform> p)
{
  MapsApp::platform = p.get();
  sceneUpdates.push_back(SceneUpdate(apiKeyScenePath, apiKey));

  // Setup style
  ImGuiIO& io = ImGui::GetIO();
  ImGui::StyleColorsDark();
  io.FontGlobalScale = 2.0f;
  //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

  // Setup tangram
  map = new Tangram::Map(std::move(p));
  map->setupGL();

  mapsSources = std::make_unique<MapsSources>(this, baseDir + "mapsources.yaml");
  mapsOffline = std::make_unique<MapsOffline>(this);

  // default position: Alamo Square, SF - overriden by scene camera position if async load
  map->setPickRadius(1.0f);
  map->setZoom(15);
  map->setPosition(-122.434668, 37.776444);
}

MapsApp::~MapsApp() { delete map; }

void MapsApp::drawFrame()  //int w, int h, int display_w, int display_h, double current_time, bool focused)
{
  static double lastFrameTime = 0;
  if(show_gui) {
#if PLATFORM_MOBILE
    ImGui_ImplGeneric_NewFrame(int w, int h, int display_w, display_h, double current_time)
    ImGui_ImplGeneric_UpdateMousePosAndButtons(last_x, last_y, focused)
    //ImGui_ImplGeneric_UpdateMouseCursor();
#endif
    ImGui::NewFrame();
    showGUI();  // ImGui::ShowDemoWindow();
  }

  // Render
  auto t0 = std::chrono::high_resolution_clock::now();
  double currTime = std::chrono::duration<double>(t0.time_since_epoch()).count();
  MapState state = map->update(currTime - lastFrameTime);
  lastFrameTime = currTime;
  if (state.isAnimating()) {
    map->getPlatform().requestRender();
  }

  map->render();

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


void MapsApp::showSceneGUI()
{
    // always show map position ... what's the difference between getPosition/getZoom and getCameraPosition()?
    double lng, lat;
    map->getPosition(lng, lat);
    ImGui::Text("lat,lng,zoom: %.7f, %.7f z%.2f", lat, lng, map->getZoom());

    if (ImGui::CollapsingHeader("Scene")) {
        if (ImGui::InputText("Scene URL", &sceneFile, ImGuiInputTextFlags_EnterReturnsTrue)) {
            loadSceneFile();
        }
        if (ImGui::InputText("API key", &apiKey, ImGuiInputTextFlags_EnterReturnsTrue)) {
          if (!apiKey.empty()) {
              sceneUpdates.push_back(SceneUpdate(apiKeyScenePath, apiKey));
          }
          loadSceneFile(false);  //, {SceneUpdate{apiKeyScenePath, apiKey}});
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
        EdgePadding padding = map->getPadding();
        if (ImGui::InputInt4("Left/Top/Right/Bottom", &padding.left)) {
            map->setPadding(padding);
        }
    }
}

void MapsApp::showDebugFlagsGUI()
{
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
    }
}

void MapsApp::showSceneVarsGUI()
{
  if (ImGui::CollapsingHeader("Scene Variables", ImGuiTreeNodeFlags_DefaultOpen)) {
    for(int ii = 0; ii < 100; ++ii) {
      std::string name = map->readSceneValue(fstring("global.gui_variables#%d.name", ii));
      if(name.empty()) break;
      std::string label = map->readSceneValue(fstring("global.gui_variables#%d.label", ii));
      std::string reload = map->readSceneValue(fstring("global.gui_variables#%d.reload", ii));

      std::string stylename = map->readSceneValue(fstring("global.gui_variables#%d.style", ii));
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
        std::string value = map->readSceneValue("global." + name);
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

void MapsApp::showGUI()
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
  showPickLabelGUI();
}

// rasterizing SVG markers

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

// create image w/ dimensions w,h from SVG string svg and upload to scene as texture with name texname
bool MapsApp::textureFromSVG(const char* texname, char* svg, float scale)
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

  TextureOptions texoptions;
  texoptions.displayScale = 1/pixel_scale;
  map->getScene()->sceneTextures().add(texname, w, h, img.data(), texoptions);
  return true;
}
