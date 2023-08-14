#include "mapsapp.h"
#include "tangram.h"
#include "scene/scene.h"
//#include "style/style.h"  // for making uniforms avail as GUI variables
#include "resources.h"
#include "util.h"
//#include "imgui.h"
//#include "imgui_stl.h"
#include "glm/common.hpp"
//#include "rapidxml/rapidxml.hpp"
#include "pugixml.hpp"
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
#include "usvg/svgpainter.h"
#include "mapwidgets.h"

static const char* apiKeyScenePath = "+global.sdk_api_key";

Platform* MapsApp::platform = NULL;
std::string MapsApp::baseDir;
//std::string MapsApp::apiKey;
CameraPosition MapsApp::mapCenter;
YAML::Node MapsApp::config;
std::string MapsApp::configFile;
bool MapsApp::metricUnits = true;
sqlite3* MapsApp::bkmkDB = NULL;
static Tooltips tooltipsInst;

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
  // geometry must be set before properties for new marker!
  map->markerSetPoint(pickResultMarker, pos);
  map->markerSetProperties(pickResultMarker, {{{"priority", priority}, {"name", namestr}}});

  // ensure marker is visible
  double scrx, scry;
  if(!map->lngLatToScreenPosition(pos.longitude, pos.latitude, &scrx, &scry))
    map->flyTo(CameraPosition{pos.longitude, pos.latitude, 16}, 1.0);  // max(map->getZoom(), 14)

  // show place info panel
  pickResultCoord = pos;
  pickResultProps.CopyFrom(props, pickResultProps.GetAllocator());

  std::string osmid = osmIdFromProps(props);

  gui->deleteContents(infoContent);  //, ".listitem");

  Widget* item = new Widget(placeInfoProto->clone());

  //SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
  SvgText* titlenode = static_cast<SvgText*>(infoPanel->containerNode()->selectFirst(".panel-title"));
  titlenode->setText(props.IsObject() && props.HasMember("name") ? props["name"].GetString() : namestr.c_str());
  titlenode->setText(SvgPainter::breakText(titlenode, 250).c_str());

  SvgText* coordnode = static_cast<SvgText*>(item->containerNode()->selectFirst(".lnglat-text"));
  if(coordnode)
    coordnode->addText(fstring("%.6f, %.6f", pos.latitude, pos.longitude).c_str());

  SvgText* distnode = static_cast<SvgText*>(item->containerNode()->selectFirst(".dist-text"));
  double distkm = lngLatDist(currLocation.lngLat(), pos);
  if(distnode)
    distnode->addText(fstring("%.1f km", distkm).c_str());

  // get place type
  auto jit = props.FindMember("tourism");
  if(jit == props.MemberEnd()) jit = props.FindMember("leisure");
  if(jit == props.MemberEnd()) jit = props.FindMember("amenity");
  if(jit == props.MemberEnd()) jit = props.FindMember("historic");
  if(jit == props.MemberEnd()) jit = props.FindMember("shop");
  if(jit != props.MemberEnd()) {
    std::string val = jit->value.GetString();
    val[0] = std::toupper(val[0]);
    SvgText* placenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".place-text"));
    if(placenode)
      placenode->addText(val.c_str());
  }

  Widget* bkmkSection = mapsBookmarks->getPlaceInfoSection(osmid, pos);
  if(bkmkSection)
    item->selectFirst(".bkmk-section")->addWidget(bkmkSection);

  //SvgContainerNode* imghost = item->selectFirst(".image-container")->containerNode();
  //imghost->addChild(resultIconNode->clone());
  infoContent->addWidget(item);

  if(!pluginManager->placeFns.empty()) {
    std::vector<std::string> cproviders = {"None"};
    for(auto& fn : pluginManager->placeFns)
      cproviders.push_back(fn.title.c_str());

    ComboBox* providerSel = createComboBox(cproviders);
    providerSel->setIndex(placeInfoProviderIdx);
    providerSel->onChanged = [=](const char*){
      placeInfoProviderIdx = providerSel->index();
      gui->deleteContents(infoContent->selectFirst(".info-section"), ".listitem");
      if(placeInfoProviderIdx > 0)
        pluginManager->jsPlaceInfo(placeInfoProviderIdx - 1, osmid);

    };
    infoContent->selectFirst(".info-section")->addWidget(createTitledRow("Information from ", providerSel));
    providerSel->onChanged("");
  }

  showPanel(infoPanel, true);
}

void MapsApp::addPlaceInfo(const char* icon, const char* title, const char* value)
{
  static const char* rowProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g class="child-container" layout="flex" flex-direction="row" box-anchor="hfill">
        <g class="image-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=""/>
        </g>
        <g class="value-container" box-anchor="hfill" layout="box" margin="0 10"></g>
      </g>
    </g>
  )";
  static std::unique_ptr<SvgNode> rowProto;
  if(!rowProto)
    rowProto.reset(loadSVGFragment(rowProtoSVG));

  Widget* row = new Widget(rowProto->clone());
  Widget* content = new Widget(row->containerNode()->selectFirst(".value-container"));
  static_cast<SvgUse*>(row->containerNode()->selectFirst(".icon"))->setTarget(MapsApp::uiIcon(icon));
  infoContent->selectFirst(".info-section")->addWidget(row);

  if(value[0] == '<') {
    SvgNode* node = loadSVGFragment(value);
    if(!node) {
      LOGE("Error parsing SVG from plugin: %s", value);
      return;
    }
    SvgContainerNode* g = node->asContainerNode();
    if(g) {
      g->setAttribute("box-anchor", "hfill");
      for(SvgNode* a : g->select("a")) {
        Button* b = new Button(a);
        b->onClicked = [b](){
          MapsApp::openURL(b->node->getStringAttr("href", b->node->getStringAttr("xlink:href")));
        };
      }
      SvgNode* textNode = g->selectFirst("text");
      if(textNode) {
        TextLabel* textbox = new TextLabel(textNode);
        // or should we require plugin set box-anchor?
        if(!strchr(textbox->text().c_str(), '\n'))
          textNode->setAttribute("box-anchor", "hfill");  // automatic ellision
      }
    }
    content->containerNode()->addChild(node);
    return;
  }

  const char* split = strchr(value, '\r');
  TextLabel* textbox = new TextLabel(createTextNode(split ? std::string(value, split-value).c_str() : value));
  content->addWidget(textbox);
  if(split) {
    // collapsible section
    Widget* row2 = new Widget(rowProto->clone());
    Widget* content2 = new Widget(row2->containerNode()->selectFirst(".value-container"));
    content2->addWidget(new TextBox(createTextNode(split+1)));

    Button* expandBtn = createToolbutton(MapsApp::uiIcon("chevron-down"), "Expand");
    expandBtn->onClicked = [=](){
      bool show = !row2->isVisible();
      expandBtn->setIcon(MapsApp::uiIcon(show ? "chevron-up" : "chevron-down"));
      row2->setVisible(show);
    };

    Widget* c = row->selectFirst(".child-container");
    //c->addWidget(createStretch());
    c->addWidget(expandBtn);

    row2->setVisible(false);
    infoContent->selectFirst(".info-section")->addWidget(row2);
  }
  else if(!strchr(value, '\n'))
    textbox->node->setAttribute("box-anchor", "hfill");  // automatic ellision
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
      pickResultProps.SetNull();
      if(pickResultMarker > 0)
        map->markerSetVisible(pickResultMarker, false);
      pickResultCoord = LngLat(NAN, NAN);
      //minimizePanel();  //hidePlaceInfo();
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

void MapsApp::loadSceneFile(bool setPosition)  //std::vector<SceneUpdate> updates,
{
  // sceneFile will be used iff sceneYaml is empty
  Tangram::SceneOptions options(sceneYaml, Url(sceneFile), setPosition, sceneUpdates);  //, updates};
  options.diskTileCacheSize = 256*1024*1024;  // value for size is ignored (just >0 to enable cache)
  options.diskCacheDir = baseDir + "cache/";
  options.preserveMarkers = true;
#ifdef DEBUG
  options.debugStyles = true;
#endif
  // single worker much easier to debug (alterative is gdb scheduler-locking option)
  if(config["num_tile_workers"].IsScalar())
    options.numTileWorkers = atoi(config["num_tile_workers"].Scalar().c_str());
  map->loadScene(std::move(options), load_async);
}

MapsApp::MapsApp(Tangram::Map* _map) : map(_map), touchHandler(new TouchHandler(this))
{
  //sceneUpdates.push_back(SceneUpdate(apiKeyScenePath, apiKey));

  // make sure cache folder exists
  mkdir(FSPath(baseDir, "cache").c_str(), 0777);

  // Setup UI panels
  mapsSources = std::make_unique<MapsSources>(this);
  mapsOffline = std::make_unique<MapsOffline>(this);
  pluginManager = std::make_unique<PluginManager>(this, baseDir + "plugins");
  // no longer recreated when scene loaded
  mapsTracks = std::make_unique<MapsTracks>(this);
  mapsSearch = std::make_unique<MapsSearch>(this);
  mapsBookmarks = std::make_unique<MapsBookmarks>(this);

  // Scene::onReady() remains false until after first call to Map::update()!
  //map->setSceneReadyListener([this](Tangram::SceneID id, const Tangram::SceneError*) {
  //  runOnMainThread([=](){
  //    // if other panels need scene loaded event, we could send to a common widget (MapsWidget?)
  //    mapsSources->onSceneLoaded();  //sourcePanel->sdlUserEvent(gui, SCENE_LOADED);
  //  });
  //});

  // cache management
  storageTotal = config["storage"]["total"].as<int64_t>(0);
  storageOffline = config["storage"]["offline"].as<int64_t>(0);
  int64_t storageShrinkMax = config["storage"]["shrink_at"].as<int64_t>(500) * 1000000;
  int64_t storageShrinkMin = config["storage"]["shrink_to"].as<int64_t>(250) * 1000000;
  // easier to track total storage and offline map storage instead cached storage directly, since offline
  //   map download/deletion can convert some tiles between cached and offline
  platform->onNotifyStorage = [=](int64_t dtotal, int64_t doffline){
    storageTotal += dtotal;
    storageOffline += doffline;
    // write out changes to offline storage total immediately since errors here can persist; errors w/ total
    //  storage will be fixed by shrinkCache
    if(doffline)
      saveConfig();
    if(storageTotal - storageOffline > storageShrinkMax && !mapsOffline->numOfflinePending()) {
      int64_t tot = mapsSources->shrinkCache(storageShrinkMin);
      storageTotal = tot + storageOffline;  // update storage usage
    }
  };

  map->setPixelScale(pixel_scale);
  map->setPickRadius(1.0f);

  metricUnits = config["metric_units"].as<bool>(true);

  // default position: Alamo Square, SF - overriden by scene camera position if async load
  CameraPosition pos;
  pos.longitude = config["view"]["lng"].as<double>(-122.434668);
  pos.latitude = config["view"]["lat"].as<double>(37.776444);
  pos.zoom = config["view"]["zoom"].as<float>(15);
  pos.rotation = config["view"]["rotation"].as<float>(0);
  pos.tilt = config["view"]["tilt"].as<float>(0);
  map->setCameraPosition(pos);
}

MapsApp::~MapsApp()
{
#if PLATFORM_DESKTOP  // on mobile, suspend will preceed destroy
  saveConfig();
#endif
}

// note that we need to saveConfig whenever app is paused on mobile, so easiest for MapsComponents to just
//  update config as soon as change is made (vs. us having to broadcast a signal on pause)
void MapsApp::saveConfig()
{
  config["storage"]["offline"] = storageOffline;
  config["storage"]["total"] = storageTotal;

  CameraPosition pos = map->getCameraPosition();
  config["view"]["lng"] = pos.longitude;
  config["view"]["lat"] = pos.latitude;
  config["view"]["zoom"] = pos.zoom;
  config["view"]["rotation"] = pos.rotation;
  config["view"]["tilt"] = pos.tilt;

  //std::string s = YAML::Dump(config);
  YAML::Emitter emitter;
  emitter.SetStringFormat(YAML::DoubleQuoted);
  emitter << config;
  FileStream fs(configFile.c_str(), "wb");
  fs.write(emitter.c_str(), emitter.size());
}

void MapsApp::mapUpdate(double time)
{
  static double lastFrameTime = 0;
  //platform->notifyRender();
  Tangram::MapState state = map->update(time - lastFrameTime);
  lastFrameTime = time;
  if (state.isAnimating()) {
    platform->requestRender();
  }
  //LOG("MapState: %d", state.flags);

  // update map center
  mapCenter = map->getCameraPosition();
  reorientBtn->setVisible(mapCenter.tilt != 0 || mapCenter.rotation != 0);
  reorientBtn->containerNode()->selectFirst(".icon")->setTransform(Transform2D::rotating(mapCenter.rotation));

  mapsTracks->onMapChange();
  mapsBookmarks->onMapChange();
  mapsOffline->onMapChange();
  mapsSources->onMapChange();
  mapsSearch->onMapChange();
  pluginManager->onMapChange();
}

void MapsApp::onResize(int wWidth, int wHeight, int fWidth, int fHeight)
{
  float new_density = (float)fWidth / (float)wWidth;
  if (new_density != density) {
    //recreate_context = true;
    density = new_density;
  }
  map->setPixelScale(pixel_scale*density);
  map->resize(fWidth, fHeight);
}

void MapsApp::updateLocation(const Location& _loc)
{
  currLocation = _loc;
  if(currLocation.time <= 0)
    currLocation.time = mSecSinceEpoch()/1000;
  if(!locMarker) {
    locMarker = map->markerAdd();
    map->markerSetStylingFromString(locMarker, locMarkerStyleStr);
    map->markerSetDrawOrder(locMarker, INT_MAX);
  }
  //map->markerSetVisible(locMarker, true);
  map->markerSetPoint(locMarker, currLocation.lngLat());

  mapsTracks->updateLocation(currLocation);
}

void MapsApp::updateGpsStatus(int satsVisible, int satsUsed)
{
  // only show if no fix yet
  gpsStatusBtn->setVisible(!satsUsed);
  if(!satsUsed)
    gpsStatusBtn->setText(fstring("%d/%d", satsUsed, satsVisible).c_str());
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
  //app->map->render();
}

Window* MapsApp::createGUI()
{
#if PLATFORM_MOBILE
  static const char* mainWindowSVG = R"#(
    <svg class="window" layout="box">
      <g class="window-layout" box-anchor="fill" layout="flex" flex-direction="column">
        <g id="maps-container" box-anchor="fill" layout="box"></g>
        <rect id="panel-splitter" class="background splitter" display="none" box-anchor="hfill" width="10" height="10"/>
        <g id="panel-container" display="none" box-anchor="hfill" layout="box">
          <rect class="background" box-anchor="fill" x="0" y="0" width="20" height="20" />
          <rect id="results-split-sizer" fill="none" box-anchor="hfill" width="320" height="200"/>
          <g id="panel-content" box-anchor="fill" layout="box"></g>
        </g>
        <g id="main-tb-container" box-anchor="hfill" layout="box"></g>
      </g>
    </svg>
  )#";
#else
  static const char* mainWindowSVG = R"#(
    <svg class="window" layout="box">
      <g id="maps-container" box-anchor="fill" layout="box"></g>
      <g class="panel-layout" box-anchor="top left" margin="10 0 0 10" layout="flex" flex-direction="column">
        <g id="main-tb-container" box-anchor="hfill" layout="box"></g>
        <g id="panel-container" display="none" box-anchor="hfill" layout="box">
          <rect class="background" x="0" y="0" width="300" height="800"/>
          <g id="panel-content" box-anchor="fill" layout="box"></g>
        </g>
      </g>
    </svg>
  )#";
#endif

  static const char* placeInfoProtoSVG = R"#(
    <g layout="flex" flex-direction="column" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g layout="flex" flex-direction="row" box-anchor="left">
        <text class="place-text" margin="0 10" font-size="14"></text>
        <rect class="stretch" fill="none" box-anchor="fill" width="20" height="20"/>
        <!-- text class="lnglat-text weak" margin="0 10" font-size="12"></text -->
        <text class="dist-text" margin="0 10" font-size="14"></text>
      </g>
      <g class="bkmk-section" layout="box" box-anchor="hfill"></g>
      <g class="info-section" layout="flex" flex-direction="column" box-anchor="hfill"></g>
    </g>
  )#";

  static const char* gpsStatusSVG = R"#(
    <g class="gps-status-button" layout="box" box-anchor="hfill">
      <rect class="background" box-anchor="hfill" width="36" height="22"/>
      <g layout="flex" flex-direction="row" box-anchor="fill">
        <g class="image-container" margin="1 2">
          <use class="icon" width="18" height="18" xlink:href=":/ui-icons.svg#satellite"/>
        </g>
        <text class="title" margin="0 4"></text>
      </g>
    </g>
  )#";

  placeInfoProto.reset(loadSVGFragment(placeInfoProtoSVG));

  Tooltips::inst = &tooltipsInst;

  SvgDocument* winnode = createWindowNode(mainWindowSVG);
  Window* win = new Window(winnode);

#if PLATFORM_MOBILE
  panelSplitter = new Splitter(winnode->selectFirst("#panel-splitter"),
      winnode->selectFirst("#results-split-sizer"), Splitter::BOTTOM, 120);
#else
  // adjust map center to account for sidebar
  Tangram::EdgePadding padding = {0,0,0,0};  //{200, 0, 0, 0};
  map->setPadding(padding);
#endif
  panelContainer = win->selectFirst("#panel-container");
  panelContent = win->selectFirst("#panel-content");

  mapsWidget = new MapsWidget(this);
  mapsWidget->node->setAttribute("box-anchor", "fill");
  mapsWidget->isFocusable = true;

  infoContent = new Widget(loadSVGFragment(R"#(<g layout="box" box-anchor="hfill"></g>)#"));  //createColumn(); //createListContainer();
  auto infoHeader = createPanelHeader(NULL, "");  //MapsApp::uiIcon("pin"), "");
  infoPanel = createMapPanel(infoHeader, infoContent);

  // toolbar w/ buttons for search, bookmarks, tracks, sources
  Menubar* mainToolbar = createMenubar();  //createToolbar();
  mainToolbar->autoClose = true;
  Button* tracksBtn = mapsTracks->createPanel();
  Button* searchBtn = mapsSearch->createPanel();
  Button* sourcesBtn = mapsSources->createPanel();
  Button* bkmkBtn = mapsBookmarks->createPanel();
  Button* pluginBtn = pluginManager->createPanel();

  mainToolbar->addButton(searchBtn);
  mainToolbar->addButton(bkmkBtn);
  mainToolbar->addButton(tracksBtn);
  mainToolbar->addButton(sourcesBtn);
  mainToolbar->addButton(pluginBtn);

  // main toolbar at bottom is better than top for auto-close menus (so menu isn't obstructed by finger)
  mainTbContainer = win->selectFirst("#main-tb-container");
  mainTbContainer->addWidget(mainToolbar);

  Widget* mapsPanel = win->selectFirst("#maps-container");
  mapsPanel->addWidget(mapsWidget);

  // recenter, reorient btns
  Toolbar* floatToolbar = createVertToolbar();
  // we could switch to different orientation modes (travel direction, compass direction) w/ multiple taps
  reorientBtn = createToolbutton(MapsApp::uiIcon("compass"), "Reorient");
  reorientBtn->onClicked = [this](){
    CameraPosition camera = map->getCameraPosition();
    camera.tilt = 0;
    camera.rotation = 0;
    map->setCameraPositionEased(camera, 1.0);
  };
  reorientBtn->setVisible(false);

  Button* recenterBtn = createToolbutton(MapsApp::uiIcon("gps-location"), "Recenter");
  recenterBtn->onClicked = [this](){
    map->flyTo(CameraPosition{currLocation.lng, currLocation.lat, map->getZoom()}, 1.0);
  };

  gpsStatusBtn = new Widget(loadSVGFragment(gpsStatusSVG));
  gpsStatusBtn->setVisible(false);

  floatToolbar->addWidget(gpsStatusBtn);
  floatToolbar->addWidget(reorientBtn);
  floatToolbar->addWidget(recenterBtn);
  floatToolbar->node->setAttribute("box-anchor", "bottom right");
  floatToolbar->setMargins(0, 10, 10, 0);
  mapsPanel->addWidget(floatToolbar);

  // misc setup
  placeInfoProviderIdx = pluginManager->placeFns.size();

  return win;
}

void MapsApp::showPanel(Widget* panel, bool isSubPanel)
{
  panel->setVisible(true);
  if(!panelHistory.empty()) {
    if(panelHistory.back() == panel)
      return;
    panelHistory.back()->setVisible(false);
    if(!isSubPanel) {
      for(Widget* w : panelHistory)
        w->sdlUserEvent(gui, PANEL_CLOSED);
      panelHistory.clear();
    }
  }
  panelHistory.push_back(panel);
  //panelContainer->addWidget(panel);
  panelContainer->setVisible(true);
  if(panelSplitter) {
    panelSplitter->setVisible(true);
    mainTbContainer->setVisible(false);
  }
  panel->sdlUserEvent(gui, PANEL_OPENED);
}

// this should be a static method or standalone fn!
Toolbar* MapsApp::createPanelHeader(const SvgNode* icon, const char* title)
{
  Toolbar* toolbar = createToolbar();
  auto backBtn = createToolbutton(MapsApp::uiIcon("back"));
  backBtn->onClicked = [this](){
    if(panelHistory.empty())
      LOGE("back button clicked but panelHistory empty ... this should never happen!");
    else {
      //panelContainer->containerNode()->removeChild(panelHistory.back()->node);
      panelHistory.back()->sdlUserEvent(gui, PANEL_CLOSED);
      panelHistory.back()->setVisible(false);
      panelHistory.pop_back();
      if(!panelHistory.empty())
        panelHistory.back()->setVisible(true);
      else {
        panelContainer->setVisible(false);
        if(panelSplitter) {
          panelSplitter->setVisible(false);
          mainTbContainer->setVisible(true);
        }
      }
    }
  };
  toolbar->addWidget(backBtn);
  Widget* titleWidget = new Widget(widgetNode("#panel-header-title"));
  if(icon)
    static_cast<SvgUse*>(titleWidget->containerNode()->selectFirst(".icon"))->setTarget(icon);
  static_cast<SvgText*>(titleWidget->containerNode()->selectFirst("text"))->setText(title);
  toolbar->addWidget(titleWidget);

  Widget* stretch = createStretch();
  if(panelSplitter) {
    stretch->addHandler([this](SvgGui* gui, SDL_Event* event) {
      if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
        panelSplitter->sdlEvent(gui, event);
        return true;
      }
      return false;
    });
  }

  toolbar->addWidget(stretch);
  return toolbar;
}

Button* MapsApp::createPanelButton(const SvgNode* icon, const char* title, Widget* panel)
{
  static const char* protoSVG = R"#(
    <g id="toolbutton" class="toolbutton" layout="box">
      <rect class="background" box-anchor="hfill" width="36" height="42"/>
      <rect class="checkmark" box-anchor="bottom hfill" margin="0 2" fill="none" width="36" height="3"/>
      <g margin="0 3" box-anchor="fill" layout="flex" flex-direction="column">
        <use class="icon" width="36" height="36" xlink:href="" />
        <text class="title" display="none" margin="0 9"></text>
      </g>
    </g>
  )#";
  static std::unique_ptr<SvgNode> proto;
  if(!proto)
    proto.reset(loadSVGFragment(protoSVG));

  Button* btn = new Button(proto->clone());
  btn->setIcon(icon);
  btn->setTitle(title);
  setupTooltip(btn, title);

  panel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE)
      btn->setChecked(true);
    else if(event->type == MapsApp::PANEL_CLOSED)
      btn->setChecked(false);
    return false;  // continue to next handler
  });

  btn->onClicked = [=](){
    if(btn->isChecked()) {
      if(!panelContainer->isVisible()) {
        panelContainer->setVisible(true);
        if(panelSplitter) {
          panelSplitter->setVisible(true);
          mainTbContainer->setVisible(false);
        }
      }
    }
    else
      showPanel(panel);
  };

  return btn;
}

Widget* MapsApp::createMapPanel(Toolbar* header, Widget* content, Widget* fixedContent, bool canMinimize)
{
  // what about just swiping down to minimize instead of a button?
  if(canMinimize) {
    auto minimizeBtn = createToolbutton(MapsApp::uiIcon(PLATFORM_MOBILE ? "chevron-down" : "chevron-up"));
    minimizeBtn->onClicked = [this](){
      // options:
      // 1. hide container and splitter and show a floating restore btn
      // 2. use setSplitSize() to shrink panel to toolbar height
      panelContainer->setVisible(false);
      if(panelSplitter) {
        panelSplitter->setVisible(false);
        mainTbContainer->setVisible(true);
      }
    };
    header->addWidget(minimizeBtn);
  }

  Widget* panel = createColumn();
  panel->node->setAttribute("box-anchor", "fill");
  panel->addWidget(header);
  if(fixedContent)
    panel->addWidget(fixedContent);
  if(content) {
    content->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only
    auto scrollWidget = new ScrollWidget(new SvgDocument(), content);
    scrollWidget->node->setAttribute("box-anchor", "fill");
    panel->addWidget(scrollWidget);
  }

  panel->setVisible(false);
  panelContainer->addWidget(panel);
  return panel;
}

//enum MessageType {Info, Question, Warning, Error};
void MapsApp::messageBox(std::string title, std::string message,
    std::vector<std::string> buttons, std::function<void(std::string)> callback)
{
  // copied from syncscribble
  Dialog* dialog = createDialog(title.c_str());
  Widget* dialogBody = dialog->selectFirst(".body-container");

  for(size_t ii = 0; ii < buttons.size(); ++ii) {
    Button* btn = dialog->addButton(buttons[ii].c_str(), [=](){ dialog->finish(int(ii)); });
    if(ii == 0)
      dialog->acceptBtn = btn;
    if(ii + 1 == buttons.size())
      dialog->cancelBtn = btn;
  }

  dialogBody->setMargins(10);
  SvgText* msgNode = createTextNode(message.c_str());
  dialogBody->addWidget(new Widget(msgNode));
  // wrap message text as needed
  std::string bmsg = SvgPainter::breakText(msgNode, 0.8*gui->windows.front()->winBounds().width());
  if(bmsg != message) {
    msgNode->clearText();
    msgNode->addText(bmsg.c_str());
  }

  dialog->onFinished = [=](int res){
    if(callback)
      callback(buttons[res]);
    //delete dialog;  //gui->deleteWidget(dialog);  -- Window deletes node
    SvgGui::delayDeleteWin(dialog);  // immediate deletion causes crash when closing link dialog by dbl click
  };
  gui->showModal(dialog, gui->windows.front()->modalOrSelf());
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

#include "sqlite3/sqlite3.h"

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

static std::string uiIconStr;

// SVG for icons
//#define LOAD_RES_FN loadIconRes
//#include "scribbleres/res_icons.cpp"

#include "ugui/theme.cpp"

static const char* moreCSS = R"#(
.listitem.checked { fill: var(--checked); }
)#";

static const char* moreWidgetSVG = R"#(
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
  <g id="listitem-icon" class="image-container" margin="2 5">
    <use class="listitem-icon icon" width="36" height="36" xlink:href=""/>
  </g>

  <g id="listitem-text-2" layout="box" box-anchor="fill">
    <text class="title-text" box-anchor="hfill" margin="0 10"></text>
    <text class="note-text weak" box-anchor="hfill bottom" margin="0 10" font-size="12"></text>
  </g>

  <g id="panel-header-title" margin="0 3" layout="flex" flex-direction="row">
    <use class="panel-icon icon" width="36" height="36" xlink:href="" />
    <text class="panel-title" margin="0 9"></text>
  </g>
</svg>
)#";

void glfwSDLEvent(SDL_Event* event)
{
  event->common.timestamp = SDL_GetTicks();
  if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_PRINTSCREEN)
    SvgGui::debugLayout = true;
  else if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F12)
    SvgGui::debugDirty = !SvgGui::debugDirty;
  else
    MapsApp::gui->sdlEvent(event);
}

static bool timerEventFired = false;

void PLATFORM_WakeEventLoop()
{
  timerEventFired = true;
  glfwPostEmptyEvent();
}

static std::mutex taskQueueMutex;
static std::vector< std::function<void()> > taskQueue;

void MapsApp::runOnMainThread(std::function<void()> fn)
{
  std::lock_guard<std::mutex> lock(taskQueueMutex);
  taskQueue.push_back(fn);
  glfwPostEmptyEvent();
}

SvgNode* MapsApp::uiIcon(const char* id)
{
  SvgNode* res = SvgGui::useFile(":/ui-icons.svg")->namedNode(id);
  ASSERT(res && "UI icon missing!");
  return res;
}

bool MapsApp::openURL(const char* url)
{
#if PLATFORM_WIN
  HINSTANCE result = ShellExecute(0, 0, PLATFORM_STR(url), 0, 0, SW_SHOWNORMAL);
  // ShellExecute returns a value greater than 32 if successful
  return (int)result > 32;
#elif PLATFORM_ANDROID
  AndroidHelper::openUrl(url);
  return true;
#elif PLATFORM_IOS
  if(!strchr(url, ':'))
    iosOpenUrl((std::string("http://") + url).c_str());
  else
    iosOpenUrl(url);
  return true;
#elif PLATFORM_OSX
  return strchr(url, ':') ? macosOpenUrl(url) : macosOpenUrl((std::string("http://") + url).c_str());
#elif IS_DEBUG
  PLATFORM_LOG("openURL: %s\n", url);
  return true;
#else  // Linux
  system(fstring("xdg-open '%s' || x-www-browser '%s' &", url, url).c_str());
  return true;
#endif
}

int main(int argc, char* argv[])
{
  using Tangram::DebugFlags;

  bool runApplication = true;
#if PLATFORM_WIN
  SetProcessDPIAware();
  winLogToConsole = attachParentConsole();  // printing to old console is slow, but Powershell is fine
#endif

  // config
  MapsApp::baseDir = "/home/mwhite/maps/";  //argc > 0 ? FSPath(argv[0]).parentPath() : ".";
  FSPath configPath(MapsApp::baseDir, "tangram-es/scenes/config.yaml");
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

  Url baseUrl("file:///");
  char pathBuffer[PATH_MAX] = {0};
  if (getcwd(pathBuffer, PATH_MAX) != nullptr) {
      baseUrl = baseUrl.resolve(Url(std::string(pathBuffer) + "/"));
  }
  LOG("Base URL: %s", baseUrl.string().c_str());
  //Url sceneUrl = baseUrl.resolve(Url(sceneFile));

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

  int nvgFlags = NVG_AUTOW_DEFAULT;  // | (Painter::sRGB ? NVG_SRGB : 0);
  //int nvglFBFlags = NVG_IMAGE_SRGB;

  NVGcontext* nvgContext = nvgswCreate(nvgFlags);  // | NVG_DEBUG);
  if(!nvgContext) { PLATFORM_LOG("Error creating nanovg context.\n"); return -1; }

  glfwSwapInterval(0);
  glfwSetTime(0);

  Painter::sharedVg = nvgContext;
  Painter::loadFont("sans", "/home/mwhite/maps/tangram-es/scenes/fonts/roboto-regular.ttf");

  // hook to support loading from resources; can we move this somewhere to deduplicate w/ other projects?
  SvgParser::openStream = [](const char* name) -> std::istream* {
    if(name[0] == ':' && name[1] == '/') {
      const char* res = getResource(name + 2);
      if(res)
        return new std::istringstream(res);
      name += 2;  //return new std::ifstream(PLATFORM_STR(FSPath(basepath, name+2).c_str()), std::ios_base::in | std::ios_base::binary);
    }
    return new std::ifstream(PLATFORM_STR(name), std::ios_base::in | std::ios_base::binary);
  };

  //loadIconRes();
  // we could just use SvgGui::useFile directly; presumably, ui-icons will eventually be embedded in exe
  uiIconStr = readFile("/home/mwhite/maps/tangram-es/app/res/ui-icons.svg");
  addStringResources({{"ui-icons.svg", uiIconStr.c_str()}});

  SvgCssStylesheet* styleSheet = new SvgCssStylesheet;
  styleSheet->parse_stylesheet(defaultStyleCSS);
  styleSheet->parse_stylesheet(moreCSS);
  styleSheet->sort_rules();
  SvgDocument* widgetDoc = SvgParser().parseString(defaultWidgetSVG);

  std::unique_ptr<SvgDocument> moreWidgets(SvgParser().parseString(moreWidgetSVG));
  for(SvgNode* node : moreWidgets->children())
    widgetDoc->addChild(node);
  moreWidgets->children().clear();

  widgetDoc->removeChild(widgetDoc->selectFirst("defs"));
  SvgDefs* defs = new SvgDefs;
  defs->addChild(MapsApp::uiIcon("chevron-down")->clone());
  defs->addChild(MapsApp::uiIcon("chevron-left")->clone());
  defs->addChild(MapsApp::uiIcon("chevron-right")->clone());
  widgetDoc->addChild(defs, widgetDoc->firstChild());

  setGuiResources(widgetDoc, styleSheet);
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

  //char* apiKey = getenv("NEXTZEN_API_KEY");
  //MapsApp::apiKey = apiKey ? apiKey : "";
  Tangram::Map* tangramMap = new Tangram::Map(std::make_unique<Tangram::LinuxPlatform>());
  MapsApp::platform = &tangramMap->getPlatform();
  MapsApp* app = new MapsApp(tangramMap);

  // debug flags can be set in config file or from command line
  auto debugNode = app->config["debug"];
  if(debugNode.IsMap()) {
    setDebugFlag(DebugFlags::freeze_tiles, debugNode["freeze_tiles"].as<bool>(false));
    setDebugFlag(DebugFlags::proxy_colors, debugNode["proxy_colors"].as<bool>(false));
    setDebugFlag(DebugFlags::tile_bounds, debugNode["tile_bounds"].as<bool>(false));
    setDebugFlag(DebugFlags::tile_infos, debugNode["tile_infos"].as<bool>(false));
    setDebugFlag(DebugFlags::labels, debugNode["labels"].as<bool>(false));
    setDebugFlag(DebugFlags::tangram_infos, debugNode["tangram_infos"].as<bool>(false));
    setDebugFlag(DebugFlags::draw_all_labels, debugNode["draw_all_labels"].as<bool>(false));
    setDebugFlag(DebugFlags::tangram_stats, debugNode["tangram_stats"].as<bool>(false));
    setDebugFlag(DebugFlags::selection_buffer, debugNode["selection_buffer"].as<bool>(false));
  }
  app->map->setupGL();

  // DB setup
  std::string dbPath = MapsApp::baseDir + "places.sqlite";
  if(sqlite3_open_v2(dbPath.c_str(), &MapsApp::bkmkDB, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
    logMsg("Error creating %s", dbPath.c_str());
    sqlite3_close(MapsApp::bkmkDB);
    MapsApp::bkmkDB = NULL;
  }

  // GUI setup
  // fake location updates to test track recording
  auto locFn = [&](){
    real lat = app->currLocation.lat + 0.0001*(0.5 + std::rand()/real(RAND_MAX));
    real lng = app->currLocation.lng + 0.0001*(0.5 + std::rand()/real(RAND_MAX));
    real alt = app->currLocation.alt + 10*std::rand()/real(RAND_MAX);
    app->updateLocation(Location{mSecSinceEpoch()/1000.0, lat, lng, 0, alt, 0, 0, 0, 0, 0, 0});
  };

  Timer* locTimer = NULL;
  Window* win = app->createGUI();
  win->sdlWindow = (SDL_Window*)glfwWin;
  win->addHandler([&](SvgGui*, SDL_Event* event){
    if(event->type == SDL_QUIT || (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE))
      runApplication = false;
    else if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_INSERT) {
      if(locTimer) {
        gui->removeTimer(locTimer);
        locTimer = NULL;
      }
      else
        locTimer = gui->setTimer(2000, win, [&](){ MapsApp::runOnMainThread(locFn); return 2000; });

    }
    else if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F5) {
      app->loadSceneFile();  // reload scene
    }
    return false;
  });
  gui->showWindow(win, NULL);

  if(sceneFile) {
    app->sceneFile = baseUrl.resolve(Url(sceneFile)).string();  //"import:\n  - " +
    app->loadSceneFile();
  }
  else
    app->mapsSources->rebuildSource(app->config["last_source"].Scalar());

  // Alamo square
  app->updateLocation(Location{0, 37.776444, -122.434668, 0, 100, 0, 0, 0, 0, 0, 0});

  while(runApplication) {
    app->needsRender() ? glfwPollEvents() : glfwWaitEvents();
    if(timerEventFired) {
      timerEventFired = false;
      SDL_Event event = {0};
      event.type = SvgGui::TIMER;
      gui->sdlEvent(&event);
    }

    {
      std::lock_guard<std::mutex> lock(taskQueueMutex);
      while(!taskQueue.empty()) {
        taskQueue.back()();
        taskQueue.pop_back();
      }
    }

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
    painter->deviceRect = Rect::wh(fbWidth, fbHeight);

    if(MapsApp::platform->notifyRender()) {
      auto t0 = std::chrono::high_resolution_clock::now();
      double currTime = std::chrono::duration<double>(t0.time_since_epoch()).count();
      app->mapUpdate(currTime);
      app->mapsWidget->node->setDirty(SvgNode::PIXELS_DIRTY);
      app->map->render();  // map rendering moved out of layoutAndDraw since object selection (which can trigger UI changes) occurs during render!
    }

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
    nvgswSetFramebuffer(painter->vg, swFB, fbWidth, fbHeight, 0, 8, 16, 24);

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

  sqlite3_close(MapsApp::bkmkDB);
  gui->closeWindow(win);
  delete gui;
  delete painter;
  delete app;
  delete tangramMap;
  nvgswuDeleteBlitter(swBlitter);
  nvgswDelete(nvgContext);
  glfwTerminate();
  return 0;
}
#endif

// rasterizing SVG markers (previous nanosvg impl removed 2023-08-13)

namespace Tangram {

bool userLoadSvg(char* svg, Texture* texture)
{
  std::unique_ptr<SvgDocument> doc(SvgParser().parseString(svg));
  if(!doc) return false;

  if(doc->hasClass("reflow-icons")) {
    size_t nicons = doc->children().size();
    int nside = int(std::sqrt(nicons) + 0.5);
    int ii = 0;
    real rowheight = 0;
    SvgDocument* prev = NULL;
    for(SvgNode* child : doc->children()) {
      if(child->type() != SvgNode::DOC) continue;
      auto childdoc = static_cast<SvgDocument*>(child);
      if(prev) {
        childdoc->m_x = ii%nside ? prev->m_x + prev->width().px() : 0;
        childdoc->m_y = ii%nside ? prev->m_y : prev->m_y + rowheight;
        //childdoc->invalidate(false); ... shouldn't be necessary
      }
      rowheight = ii%nside ? std::max(rowheight, childdoc->height().px()) : childdoc->height().px();
      prev = childdoc;
      ++ii;
    }
    Rect b = doc->bounds();
    doc->setWidth(b.width());
    doc->setHeight(b.height());
  }

  int w = int(doc->width().px(96) + 0.5), h = int(doc->height().px(96) + 0.5);
  Image img(w, h);
  // this fn will be run on a thread if loading scene async, so we cannot use shared nvg context
  NVGcontext* nvgContext = nvgswCreate(NVG_AUTOW_DEFAULT | NVG_SRGB | NVGSW_PATHS_XC);
  Painter painter(&img, nvgContext);
  painter.beginFrame();
  painter.translate(0, h);
  painter.scale(1, -1);
  SvgPainter(&painter).drawNode(doc.get());  //, dirty);
  painter.endFrame();
  nvgswDelete(nvgContext);

  auto atlas = std::make_unique<SpriteAtlas>();
  bool hasSprites = false;
  for(auto pair : doc->m_namedNodes) {
    if(pair.second->type() != SvgNode::DOC) continue;
    //if(pair.second->isVisible()) continue;
    hasSprites = true;
    Rect b = pair.second->bounds();
    glm::vec2 pos(b.left, b.top);
    glm::vec2 size(b.width(), b.height());
    atlas->addSpriteNode(pair.first.c_str(), pos, size);
  }
  if(hasSprites)
    texture->setSpriteAtlas(std::move(atlas));
  texture->setPixelData(w, h, 4, img.bytes(), img.dataLen());
  return true;
}

}
