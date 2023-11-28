#include "mapsapp.h"
#include "tangram.h"
#include "scene/scene.h"
#include "util.h"
#include "glm/common.hpp"
#include "pugixml.hpp"
#include "rapidjson/document.h"
#include <sys/stat.h>
#include <fstream>
#include "nfd.h"
#include "sqlitepp.h"

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
#include "usvg/svgparser.h"
#include "mapwidgets.h"

#if !defined(DEBUG) && !defined(NDEBUG)
#error "One of DEBUG or NDEBUG must be defined!"
#endif

Platform* MapsApp::platform = NULL;
std::string MapsApp::baseDir;
YAML::Node MapsApp::config;
std::string MapsApp::configFile;
bool MapsApp::metricUnits = true;
sqlite3* MapsApp::bkmkDB = NULL;
std::vector<Color> MapsApp::markerColors;

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

LngLat MapsApp::getMapCenter()
{
  LngLat res;
  int w = map->getViewportWidth(), h = map->getViewportHeight();
  map->screenPositionToLngLat(w/2, h/2, &res.longitude, &res.latitude);
  return res;
}

// I think we'll eventually want to use a plugin for this
std::string MapsApp::osmPlaceType(const rapidjson::Document& props)
{
  if(!props.IsObject()) return {};
  auto jit = props.FindMember("tourism");
  if(jit == props.MemberEnd()) jit = props.FindMember("leisure");
  if(jit == props.MemberEnd()) jit = props.FindMember("amenity");
  if(jit == props.MemberEnd()) jit = props.FindMember("historic");
  if(jit == props.MemberEnd()) jit = props.FindMember("shop");
  if(jit == props.MemberEnd()) return {};
  std::string val = jit->value.GetString();
  val[0] = std::toupper(val[0]);
  std::replace(val.begin(), val.end(), '_', ' ');
  return val;
}

void MapsApp::setPickResult(LngLat pos, std::string namestr, const rapidjson::Document& props)  //, int priority)
{
  static const char* placeInfoProtoSVG = R"#(
    <g layout="flex" flex-direction="column" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g class="place-info-row" layout="flex" flex-direction="row" box-anchor="hfill" margin="6 0">
        <text class="place-text" margin="0 6" font-size="14"></text>
        <rect class="stretch" fill="none" box-anchor="fill" width="20" height="20"/>
        <use class="icon elevation-icon" display="none" width="18" height="18" xlink:href=":/ui-icons.svg#mountain"/>
        <text class="elevation-text" margin="0 6" font-size="14"></text>
        <use class="icon direction-icon" width="18" height="18" xlink:href=":/ui-icons.svg#arrow-narrow-up"/>
        <text class="dist-text" margin="0 6" font-size="14"></text>
      </g>
      <g class="currloc-info-row" display="none" layout="flex" flex-direction="row" box-anchor="hfill" margin="6 0">
        <text class="lnglat-text" margin="0 10" font-size="12"></text>
        <rect class="stretch" fill="none" box-anchor="fill" width="20" height="20"/>
        <use class="icon" width="18" height="18" xlink:href=":/ui-icons.svg#mountain"/>
        <text class="currloc-elevation-text" margin="0 6" font-size="14"></text>
      </g>
      <g class="action-container" layout="box" box-anchor="hfill" margin="0 3"></g>
      <g class="waypt-section" layout="box" box-anchor="hfill"></g>
      <g class="bkmk-section" layout="box" box-anchor="hfill"></g>
      <g class="info-section" layout="flex" flex-direction="column" box-anchor="hfill"></g>
    </g>
  )#";

  static std::unique_ptr<SvgNode> placeInfoProto;
  if(!placeInfoProto)
    placeInfoProto.reset(loadSVGFragment(placeInfoProtoSVG));

  std::string osmid = osmIdFromProps(props);
  pickResultCoord = pos;
  pickResultName = namestr;
  pickResultProps.CopyFrom(props, pickResultProps.GetAllocator());
  currLocPlaceInfo = (locMarker > 0 && pickedMarkerId == locMarker);
  // allow pick result to be used as waypoint
  if(mapsTracks->onPickResult())
    return;

  if(namestr.empty())
    namestr = fstring("%.6f, %.6f", pos.latitude, pos.longitude);
  // show marker
  if(pickResultMarker == 0)
    pickResultMarker = map->markerAdd();
  map->markerSetVisible(pickResultMarker, !currLocPlaceInfo);

  // ensure marker is visible
  double scrx, scry;
  if(!map->lngLatToScreenPosition(pos.longitude, pos.latitude, &scrx, &scry))
    map->flyTo(CameraPosition{pos.longitude, pos.latitude, 16}, 1.0);  // max(map->getZoom(), 14)

  // show place info panel
  gui->deleteContents(infoContent);  //, ".listitem");
  Widget* item = new Widget(placeInfoProto->clone());
  infoContent->addWidget(item);

  showPanel(infoPanel, true);
  infoPanel->selectFirst(".minimize-btn")->setVisible(panelHistory.size() > 1);

  // actions toolbar
  Toolbar* toolbar = createToolbar();
  mapsBookmarks->addPlaceActions(toolbar);
  mapsTracks->addPlaceActions(toolbar);
  toolbar->addWidget(createStretch());
  item->selectFirst(".action-container")->addWidget(toolbar);

  TextLabel* titlenode = static_cast<TextLabel*>(infoPanel->selectFirst(".panel-title"));
  titlenode->setText(namestr.c_str());
  //titlenode->setText(SvgPainter::breakText(titlenode->textNode, 250).c_str());

  Widget* bkmkSection = mapsBookmarks->getPlaceInfoSection(osmid, pos);
  if(bkmkSection)
    item->selectFirst(".bkmk-section")->addWidget(bkmkSection);

  if(currLocPlaceInfo) {
    item->selectFirst(".place-info-row")->setVisible(false);
    item->selectFirst(".currloc-info-row")->setVisible(true);
    updateLocation(currLocation);  // set lng, lat, elevation text
    return;
  }

  map->markerSetStylingFromPath(pickResultMarker, "layers.pick-marker.draw.marker");
  map->markerSetPoint(pickResultMarker, pos);  // geometry must be set before properties for new marker!
  map->markerSetProperties(pickResultMarker, {{{"name", namestr}}});  //{"priority", priority}

  Widget* distwdgt = item->selectFirst(".dist-text");
  if(distwdgt) {
    double dist = lngLatDist(currLocation.lngLat(), pos);
    double bearing = lngLatBearing(currLocation.lngLat(), pos);
    SvgUse* icon = static_cast<SvgUse*>(item->containerNode()->selectFirst(".direction-icon"));
    if(icon)
      icon->setTransform(Transform2D::rotating(bearing, icon->viewport().center()));
    distwdgt->setText(fstring(metricUnits ? "%.1f km" : "%.1f mi", metricUnits ? dist : dist*0.621371).c_str());
  }

  // get place type
  std::string placetype = osmPlaceType(props);
  SvgText* placenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".place-text"));
  if(placenode && !placetype.empty())
    placenode->addText(placetype.c_str());

   auto elevFn = [this](double elev){
    Widget* elevWidget = infoContent->selectFirst(".elevation-text");
    if(elevWidget) {
      elevWidget->setText(fstring(metricUnits ? "%.0f m" : "%.0f ft", metricUnits ? elev : elev*3.28084).c_str());
      infoContent->selectFirst(".elevation-icon")->setVisible(true);  //elevWidget->setVisible(true);
    }
  };

  if(props.IsObject() && props.HasMember("ele"))
    elevFn(props["ele"].GetDouble());
  else
    getElevation(pos, elevFn);

  if(!osmid.empty() && !pluginManager->placeFns.empty()) {
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
    Widget* providerRow = createRow();
    providerRow->node->setAttribute("margin", "3 6");
    providerRow->addWidget(new TextBox(createTextNode("Information from ")));
    providerRow->addWidget(createStretch());
    providerRow->addWidget(providerSel);
    infoContent->selectFirst(".info-section")->addWidget(providerRow);
    //infoContent->selectFirst(".info-section")->addWidget(createTitledRow("Information from ", providerSel));
    providerSel->onChanged("");
  }

  if(props.IsObject() && props.HasMember("place_info")) {
    for(auto& info : props["place_info"].GetArray())
      addPlaceInfo(info["icon"].GetString(), info["title"].GetString(), info["value"].GetString());
  }
}

void MapsApp::setPickResult(LngLat pos, std::string namestr, std::string propstr)
{
  rapidjson::Document props;
  props.Parse(propstr.c_str());
  setPickResult(pos, namestr, props);
}

void MapsApp::placeInfoPluginError(const char* err)
{
  Button* retryBtn = createToolbutton(MapsApp::uiIcon("retry"), "Retry", true);
  retryBtn->onClicked = [=](){
    std::string osmid = osmIdFromProps(pickResultProps);
    pluginManager->jsPlaceInfo(placeInfoProviderIdx - 1, osmid);
    retryBtn->setVisible(false);
  };
  infoContent->selectFirst(".info-section")->addWidget(retryBtn);
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
  infoContent->selectFirst(".info-section")->addWidget(row);

  auto iconUseNode = static_cast<SvgUse*>(row->containerNode()->selectFirst(".icon"));
  if(icon[0] == '<') {
    SvgDocument* svgDoc = SvgParser().parseString(value);
    if(svgDoc)
      iconUseNode->setTarget(svgDoc, std::shared_ptr<SvgDocument>(svgDoc));
  }
  else if(icon[0])
    iconUseNode->setTarget(MapsApp::uiIcon(icon));
  else
    row->selectFirst(".image-container")->setVisible(false);  // no icon

  if(value[0] == '<') {
    SvgNode* node = loadSVGFragment(value);
    if(!node) {
      LOGE("Error parsing SVG from plugin: %s", value);
      return;
    }
    SvgContainerNode* g = node->asContainerNode();
    if(g) {
      g->setAttribute("box-anchor", "hfill");
      auto anchorNodes = g->select("a");
      for(SvgNode* a : anchorNodes) {
        Button* b = new Button(a);
        b->onClicked = [b](){
          MapsApp::openURL(b->node->getStringAttr("href", b->node->getStringAttr("xlink:href")));
        };
      }
      auto textNodes = g->select("text");
      if(textNodes.size() == 1) {
        textNodes[0]->setAttribute("box-anchor", "left");
        SvgG* wrapper = new SvgG;
        wrapper->addChild(node);
        TextLabel* textbox = new TextLabel(wrapper);
        //if(!strchr(textbox->text().c_str(), '\n'))
        wrapper->setAttribute("box-anchor", "hfill");
        node = wrapper;
      }
    }
    else if(node->type() == SvgNode::TEXT) {
      SvgText* textnode = static_cast<SvgText*>(node);
      if(textnode->hasClass("wrap-text"))
        textnode->setText(SvgPainter::breakText(textnode, icon[0] ? 250 : 300).c_str());
      else if(textnode->hasClass("elide-text"))
        SvgPainter::elideText(textnode, icon[0] ? 250 : 300);
    }
    content->containerNode()->addChild(node);
    return;
  }

  const char* split = strchr(value, '\r');
  std::string valuestr = split ? std::string(value, split-value).c_str() : value;
  SvgText* textnode = createTextNode(valuestr.c_str());
  textnode->setAttribute("box-anchor", "left");
  //if(!strchr(valuestr.c_str(), '\n'))
  textnode->setText(SvgPainter::breakText(textnode, 250).c_str());
  content->addWidget(new TextBox(textnode));
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
}

void MapsApp::longPressEvent(float x, float y)
{
  double lng, lat;
  map->screenPositionToLngLat(x, y, &lng, &lat);
  // clear panel history unless editing track/route
  if(!mapsTracks->activeTrack)
    showPanel(infoPanel);
  setPickResult(LngLat(lng, lat), "", "");
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

void MapsApp::clearPickResult()
{
  pickResultProps.SetNull();
  if(pickResultMarker > 0)
    map->markerSetVisible(pickResultMarker, false);
  pickResultCoord = LngLat(NAN, NAN);
  currLocPlaceInfo = false;
}

void MapsApp::tapEvent(float x, float y)
{
  //LngLat location;
  map->screenPositionToLngLat(x, y, &tapLocation.longitude, &tapLocation.latitude);
#if IS_DEBUG
  double xx, yy;
  map->lngLatToScreenPosition(tapLocation.longitude, tapLocation.latitude, &xx, &yy);
  LOGD("tapEvent: %f,%f -> %f,%f (%f, %f)\n", x, y, tapLocation.longitude, tapLocation.latitude, xx, yy);
#endif

  map->pickLabelAt(x, y, [&](const Tangram::LabelPickResult* result) {
    auto& props = result->touchItem.properties;
    LOGD("Picked label: %s", result ? props->getAsString("name").c_str() : "none");
    if (!result) {
      clearPickResult();
      return;
    }
    std::string itemId = props->getAsString("id");
    std::string osmType = props->getAsString("osm_type");
    if(itemId.empty())
      itemId = props->getAsString("osm_id");
    if(osmType.empty())
      osmType = "node";
    std::string namestr = props->getAsString("name");
    // clear panel history unless editing track/route
    if(!mapsTracks->activeTrack)
      showPanel(infoPanel);    //mapsSearch->clearSearch();
    setPickResult(result->coordinates, namestr, props->toJson());
    tapLocation = {NAN, NAN};
  });

  map->pickMarkerAt(x, y, [&](const Tangram::MarkerPickResult* result) {
    if(!result || result->id == pickResultMarker)
      return;
    LOGD("Marker %d picked", result->id);
    map->markerSetVisible(pickResultMarker, false);  // ???
    pickedMarkerId = result->id;
    map->screenPositionToLngLat(
        result->position[0], result->position[1], &pickResultCoord.longitude, &pickResultCoord.latitude);
    //pickResultCoord = result->coordinates;  -- just set to center of marker for polylines/polygons
    tapLocation = {NAN, NAN};
  });

  //map->getPlatform().requestRender();
}

void MapsApp::hoverEvent(float x, float y)
{
  // ???
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

void MapsApp::loadSceneFile(bool async, bool setPosition)
{
  for(auto& upd : sceneUpdates) {
    if(upd.value.c_str()[0] == '#')  // handle problem w/ passing around colors
      upd.value = '"' + upd.value + '"';
  }
  // sceneFile will be used iff sceneYaml is empty
  Tangram::SceneOptions options(sceneYaml, Url(sceneFile), setPosition, sceneUpdates);  //, updates};
  options.diskTileCacheSize = 256*1024*1024;  // value for size is ignored (just >0 to enable cache)
  options.diskCacheDir = baseDir + "cache/";
  options.preserveMarkers = true;
#if IS_DEBUG
  options.debugStyles = true;
#endif
  // single worker much easier to debug (alternative is gdb scheduler-locking option)
  if(config["num_tile_workers"].IsScalar())
    options.numTileWorkers = atoi(config["num_tile_workers"].Scalar().c_str());
  map->loadScene(std::move(options), async);
}

MapsApp::MapsApp(Tangram::Map* _map) : map(_map), touchHandler(new TouchHandler(this))
{
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

MapsApp::~MapsApp() {}

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
  //emitter.SetStringFormat(YAML::DoubleQuoted);
  emitter << config;
  FileStream fs(configFile.c_str(), "wb");
  fs.write(emitter.c_str(), emitter.size());
}

void MapsApp::sendMapEvent(MapEvent_t event)
{
  mapsTracks->onMapEvent(event);
  mapsBookmarks->onMapEvent(event);
  //mapsOffline->onMapEvent(event);
  mapsSources->onMapEvent(event);
  mapsSearch->onMapEvent(event);
  //pluginManager->onMapEvent(event);
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
  auto cpos = map->getCameraPosition();
  reorientBtn->setVisible(cpos.tilt != 0 || cpos.rotation != 0);
  reorientBtn->containerNode()->selectFirst(".icon")->setTransform(Transform2D::rotating(cpos.rotation));

  if(pickedMarkerId == locMarker) {
    if(!mapsTracks->activeTrack)
      showPanel(infoPanel);
    setPickResult(currLocation.lngLat(), "Current location", "");
    mapsSearch->clearSearch();  // ???
    pickedMarkerId = 0;
  }

  sendMapEvent(MAP_CHANGE);
  pickedMarkerId = 0;  // prevent repeated searched for ignored marker
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

void MapsApp::onSuspend()
{
  sendMapEvent(SUSPEND);
  saveConfig();
}

void MapsApp::updateLocation(const Location& _loc)
{
  currLocation = _loc;
  if(currLocation.time <= 0)
    currLocation.time = mSecSinceEpoch()/1000;
  if(!locMarker) {
    locMarker = map->markerAdd();
    map->markerSetStylingFromPath(locMarker, "layers.loc-marker.draw.marker");
    map->markerSetDrawOrder(locMarker, INT_MAX);
  }
  //map->markerSetVisible(locMarker, true);
  map->markerSetPoint(locMarker, currLocation.lngLat());

  if(currLocPlaceInfo) {
    SvgText* coordnode = static_cast<SvgText*>(infoContent->containerNode()->selectFirst(".lnglat-text"));
    std::string locstr = fstring("%.6f, %.6f", currLocation.lat, currLocation.lng);
    if(currLocation.poserr > 0)
      locstr += fstring(" (%.0f m)", currLocation.poserr);
    if(coordnode)
      coordnode->setText(locstr.c_str());

    SvgText* elevnode = static_cast<SvgText*>(infoContent->containerNode()->selectFirst(".currloc-elevation-text"));
    double elev = currLocation.alt;
    if(elevnode)
      elevnode->setText(fstring(metricUnits ? "%.0f m" : "%.0f ft", metricUnits ? elev : elev*3.28084).c_str());
  }

  sendMapEvent(LOC_UPDATE);
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

#include "data/rasterSource.h"

static double readElevTex(Tangram::Texture* tex, int x, int y)
{
  // see getElevation() in raster-contour.yaml and https://github.com/tilezen/joerd
  GLubyte* p = tex->bufferData() + y*tex->width()*4 + x*4;
  //(red * 256 + green + blue / 256) - 32768
  return (p[0]*256 + p[1] + p[2]/256.0) - 32768;
}

static double elevationLerp(Tangram::Texture* tex, TileID tileId, LngLat pos)
{
  using namespace Tangram;

  double scale = MapProjection::metersPerTileAtZoom(tileId.z);
  ProjectedMeters tileOrigin = MapProjection::tileSouthWestCorner(tileId);
  ProjectedMeters meters = MapProjection::lngLatToProjectedMeters(pos);  //glm::dvec2(tileCoord) * scale + tileOrigin;
  ProjectedMeters offset = meters - tileOrigin;
  double ox = offset.x/scale, oy = offset.y/scale;
  if(ox < 0 || ox > 1 || oy < 0 || oy > 1)
    LOGE("Elevation tile position out of range");
  // ... seems this work correctly w/o accounting for vertical flip of texture
  double x0 = ox*tex->width(), y0 = oy*tex->height();
  int ix0 = std::floor(x0), iy0 = std::floor(y0);
  int ix1 = std::ceil(x0), iy1 = std::ceil(y0);
  double fx = x0 - ix0, fy = y0 - iy0;
  double t00 = readElevTex(tex, ix0, iy0);
  double t01 = readElevTex(tex, ix0, iy1);
  double t10 = readElevTex(tex, ix1, iy0);
  double t11 = readElevTex(tex, ix1, iy1);
  double t0 = t00 + fx*(t10 - t00);
  double t1 = t01 + fx*(t11 - t01);
  return t0 + fy*(t1 - t0);
}

void MapsApp::getElevation(LngLat pos, std::function<void(double)> callback)
{
  using namespace Tangram;

  static std::unique_ptr<Texture> prevTex;
  static TileID prevTileId = {0, 0, 0, 0};

  if(prevTex) {
    TileID tileId = lngLatTile(pos, prevTileId.z);
    if(tileId == prevTileId) {
      callback(elevationLerp(prevTex.get(), tileId, pos));
      return;
    }
  }

  auto& tileSources = map->getScene()->tileSources();
  for(const auto& srcname : config["sources"]["elevation"]) {
    for(auto& src : tileSources) {
      if(src->isRaster() && src->name() == srcname.Scalar()) {
        TileID tileId = lngLatTile(pos, src->maxZoom());
        // do not use RasterSource::createTask() because we can't use its cached Textures!
        auto task = std::make_shared<BinaryTileTask>(tileId, src);  //rsrc->createTask(tileId);
        src->loadTileData(task, {[=](std::shared_ptr<TileTask> _task) {
          runOnMainThread([=](){
            if(_task->hasData()) {
              auto* rsrc = static_cast<RasterSource*>(_task->source().get());
              auto tex = rsrc->getTextureDirect(_task);
              if(tex && tex->bufferData()) {
                callback(elevationLerp(tex.get(), tileId, pos));
                prevTex = std::move(tex);
                prevTileId = tileId;
              }
            }
          });
        }});
      }
    }
  }
}

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
    //FileStream fout(filename.c_str(), "wb");
    std::ofstream fout(filename);
    std::lock_guard<std::mutex> lock(logMutex);
    auto tileData = task->source()->parse(*task);  //task->source() ? : Mvt::parseTile(*task, 0);
    std::vector<std::string> layerstr;
    for(const Layer& layer : tileData->layers) {
      std::vector<std::string> featstr;
      for(const Feature& feature : layer.features) {
        size_t ng = feature.polygons.size(), np = feature.points.size(), nl = feature.lines.size();
        std::string geom = ng ? std::to_string(ng) + " polygons" : nl ? std::to_string(nl) + " lines" : std::to_string(np) + " points";
        featstr.push_back("{ \"properties\": " + feature.props.toJson() + ", \"geometry\": \"" + geom + "\" }");
      }
      layerstr.push_back("\"" + layer.name + "\": [\n  " + joinStr(featstr, ",\n  ") + "\n]");
    }
    fout << "{" << joinStr(layerstr, ",\n\n") << "}";
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
      if(event->tfinger.touchId == SDL_TOUCH_MOUSEID && event->tfinger.fingerId != SDL_BUTTON_LMASK) {
        if(event->type == SDL_FINGERDOWN) {
          if(SDL_GetModState() & KMOD_ALT)
            app->dumpTileContents(event->tfinger.x/gui->inputScale, event->tfinger.y/gui->inputScale);
          else
            app->longPressEvent(event->tfinger.x/gui->inputScale, event->tfinger.y/gui->inputScale);
        }
      }
      else
        app->touchHandler->touchEvent(event->tfinger.touchId, actionFromSDLFinger(event->type),
             event->tfinger.timestamp/1000.0, event->tfinger.x/gui->inputScale, event->tfinger.y/gui->inputScale, 1.0f);
    }
    else if(event->type == SDL_MOUSEWHEEL) {
      Point p = gui->prevFingerPos;
      uint32_t mods = (PLATFORM_WIN || PLATFORM_LINUX) ? (event->wheel.direction >> 16) : SDL_GetModState();
      app->onMouseWheel(p.x/gui->inputScale, p.y/gui->inputScale, event->wheel.x/120.0, event->wheel.y/120.0, mods & KMOD_ALT, mods & KMOD_CTRL);
    }
    else if(event->type == SvgGui::MULTITOUCH) {
      SDL_Event* fevent = static_cast<SDL_Event*>(event->user.data1);
      auto points = static_cast<std::vector<SDL_Finger>*>(event->user.data2);
      for(const SDL_Finger& pt : *points) {
        int action = pt.id == fevent->tfinger.fingerId ? actionFromSDLFinger(fevent->type) : 0;
        app->touchHandler->touchEvent(event->tfinger.touchId, action,
            event->user.timestamp/1000.0, pt.x/gui->inputScale, pt.y/gui->inputScale, 1.0f);
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

class CrosshairWidget : public Widget
{
public:
  CrosshairWidget() : Widget(new SvgCustomNode) {}
  void draw(SvgPainter* svgp) const override;
  Rect bounds(SvgPainter* svgp) const override;
};

Rect CrosshairWidget::bounds(SvgPainter* svgp) const
{
  return svgp->p->getTransform().mapRect(Rect::wh(40, 40));
}

void CrosshairWidget::draw(SvgPainter* svgp) const
{
  Painter* p = svgp->p;
  Rect bbox = node->bounds();

  p->setFillBrush(Color::NONE);
  p->setStroke(Color::RED, 3);  //, Painter::FlatCap, Painter::BevelJoin);
  p->drawLine(Point(0, bbox.height()/2), Point(bbox.width(), bbox.height()/2));
  p->drawLine(Point(bbox.width()/2, 0), Point(bbox.width()/2, bbox.height()));
}

class ScaleBarWidget : public Widget
{
public:
  ScaleBarWidget(Map* _map) : Widget(new SvgCustomNode), map(_map) {}
  void draw(SvgPainter* svgp) const override;
  Rect bounds(SvgPainter* svgp) const override;

  Map* map;
};

Rect ScaleBarWidget::bounds(SvgPainter* svgp) const
{
  return svgp->p->getTransform().mapRect(Rect::wh(100, 20));
}

void ScaleBarWidget::draw(SvgPainter* svgp) const
{
  Painter* p = svgp->p;
  Rect bbox = node->bounds();
  //real w = bbox.width(), h = bbox.height();  p->translate(w/2, h/2);

  real y = bbox.center().y;
  LngLat r0, r1;
  map->screenPositionToLngLat(bbox.left, y, &r0.longitude, &r0.latitude);
  map->screenPositionToLngLat(bbox.right, y, &r1.longitude, &r1.latitude);

  // steps are 1, 2, 5, 10, 20, 50, ...
  const char* format = "%.0f km";
  real dist = lngLatDist(r0, r1);
  if(!MapsApp::metricUnits) {
    dist = dist*0.621371;
    if(dist < 0.1) {
      dist = 5280*dist;
      format = "%.0f ft";
    }
    else
      format = dist < 1 ? "%.1f mi" : "%.0f mi";
  }
  else if(dist < 1) {
    dist *= 1000;
    format = "%.0f m";
  }
  real pow10 = std::pow(10, std::floor(std::log10(dist)));
  real firstdigit = dist/pow10;
  real n = firstdigit < 2 ? 1 : firstdigit < 5 ? 2 : 5;
  real scaledist = n * pow10;
  std::string str = fstring(format, scaledist);
#if IS_DEBUG
  str += fstring("  (z%.2f)", map->getZoom());
#endif

  real y0 = bbox.height()/2;
  p->setFillBrush(Color::NONE);
  p->setStroke(Color::WHITE, 3);  //, Painter::FlatCap, Painter::BevelJoin);
  p->drawLine(Point(0, y0), Point(bbox.width()*scaledist/dist, y0));
  p->setStroke(Color::BLACK, 2);  //, Painter::FlatCap, Painter::BevelJoin);
  p->drawLine(Point(0, y0), Point(bbox.width()*scaledist/dist, y0));
  p->setFontSize(14);
  p->setStroke(Color::WHITE, 2);
  p->drawText(0, 0, str.c_str());
  p->setFillBrush(Color::BLACK);
  p->setStroke(Color::NONE);
  p->drawText(0, 0, str.c_str());
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
        <g id="main-tb-container" box-anchor="hfill" layout="box">
          <rect class="background" fill="none" x="0" y="0" width="360" height="1"/>
        </g>
        <rect id="panel-separator" class="hrule title background" display="none" box-anchor="hfill" width="20" height="2"/>
        <g id="panel-container" display="none" box-anchor="hfill" layout="box">
          <rect class="background" box-anchor="hfill" x="0" y="0" width="20" height="800"/>
          <g id="panel-content" box-anchor="fill" layout="box"></g>
        </g>
      </g>
    </svg>
  )#";
#endif

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

  static const char* reorientSVG = R"#(
    <g class="reorient-btn toolbutton" layout="box">
      <rect class="background" width="42" height="42"/>
      <g margin="0 0" box-anchor="fill" layout="box">
        <use class="icon" width="28" height="28" xlink:href=":/ui-icons.svg#compass" />
      </g>
    </g>
  )#";

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
  panelSeparator = win->selectFirst("#panel-separator");
#endif
  panelContainer = win->selectFirst("#panel-container");
  panelContent = win->selectFirst("#panel-content");

  mapsWidget = new MapsWidget(this);
  mapsWidget->node->setAttribute("box-anchor", "fill");
  mapsWidget->isFocusable = true;

  infoContent = new Widget(loadSVGFragment(R"#(<g layout="box" box-anchor="hfill"></g>)#"));
  auto infoHeader = createPanelHeader(NULL, "");  //MapsApp::uiIcon("pin"), "");
  infoPanel = createMapPanel(infoHeader, infoContent);
  infoPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == MapsApp::PANEL_CLOSED)
      clearPickResult();
    return false;
  });

  // toolbar w/ buttons for search, bookmarks, tracks, sources
  Menubar* mainToolbar = createMenubar();  //createToolbar();
  mainToolbar->autoClose = true;
  mainToolbar->selectFirst(".child-container")->node->setAttribute("justify-content", "space-between");
  Button* tracksBtn = mapsTracks->createPanel();
  Button* searchBtn = mapsSearch->createPanel();
  Button* sourcesBtn = mapsSources->createPanel();
  Button* bkmkBtn = mapsBookmarks->createPanel();
  Button* pluginBtn = pluginManager->createPanel();

  mainToolbar->addButton(searchBtn);
  mainToolbar->addButton(bkmkBtn);
  mainToolbar->addButton(tracksBtn);
  mainToolbar->addButton(sourcesBtn);
  //mainToolbar->addButton(pluginBtn);

  Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
  Menu* overflowMenu = createMenu(Menu::VERT_RIGHT | (PLATFORM_MOBILE ? Menu::ABOVE : 0));
  overflowBtn->setMenu(overflowMenu);
  overflowMenu->addItem(pluginBtn);
  Button* metricCb = createCheckBoxMenuItem("Use metric units");
  metricCb->onClicked = [=](){
    metricUnits = !metricUnits;
    config["metric_units"] = metricUnits;
    metricCb->setChecked(metricUnits);
    mapsSources->rebuildSource(mapsSources->currSource);  //loadSceneFile();
  };
  metricCb->setChecked(metricUnits);
  overflowMenu->addItem(metricCb);

  Menu* debugMenu = createMenu(Menu::HORZ);
  const char* debugFlags[9] = {"Freeze tiles", "Proxy colors", "Tile bounds",
      "Tile info", "Labels", "Tangram Info", "Draw all labels", "Tangram stats", "Selection buffer"};
  for(int ii = 0; ii < 9; ++ii) {
    Button* debugCb = createCheckBoxMenuItem(debugFlags[ii]);
    debugCb->onClicked = [=](){
      debugCb->setChecked(!debugCb->isChecked());
      setDebugFlag(Tangram::DebugFlags(ii), debugCb->isChecked());
      //loadSceneFile();  -- most debug flags shouldn't require scene reload
    };
    debugMenu->addItem(debugCb);
  }
  overflowMenu->addSubmenu("Debug", debugMenu);

  mainToolbar->addButton(overflowBtn);

  // main toolbar at bottom is better than top for auto-close menus (so menu isn't obstructed by finger)
  mainTbContainer = win->selectFirst("#main-tb-container");
  mainTbContainer->addWidget(mainToolbar);

  Widget* mapsPanel = win->selectFirst("#maps-container");
  mapsPanel->addWidget(mapsWidget);

  // recenter, reorient btns
  Toolbar* floatToolbar = createVertToolbar();
  // we could switch to different orientation modes (travel direction, compass direction) w/ multiple taps
  reorientBtn = new Button(loadSVGFragment(reorientSVG));  //createToolbutton(MapsApp::uiIcon("compass"), "Reorient");
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

  ScaleBarWidget* scaleBar = new ScaleBarWidget(map);
  scaleBar->node->setAttribute("box-anchor", "bottom left");
  scaleBar->setMargins(0, 0, 10, 10);
  mapsPanel->addWidget(scaleBar);

  crossHair = new CrosshairWidget();
  crossHair->setVisible(false);
  mapsPanel->addWidget(crossHair);

  legendContainer = createColumn();
  legendContainer->node->setAttribute("box-anchor", "bottom");
  legendContainer->node->addClass("legend");
  legendContainer->setMargins(0, 0, 10, 0);
  mapsPanel->addWidget(legendContainer);

  // misc setup
  placeInfoProviderIdx = pluginManager->placeFns.size();

  return win;
}

void MapsApp::showPanelContainer(bool show)
{
  panelContainer->setVisible(show);
  if(panelSeparator)
    panelSeparator->setVisible(show);
  if(panelSplitter) {
    panelSplitter->setVisible(show);
    mainTbContainer->setVisible(!show);
  }
}

void MapsApp::showPanel(Widget* panel, bool isSubPanel)
{
  if(!panelHistory.empty()) {
    if(panelHistory.back() == panel) {
      panel->setVisible(true);
      return;
    }
    panelHistory.back()->setVisible(false);
    if(!isSubPanel) {
      for(Widget* w : panelHistory)
        w->sdlUserEvent(gui, PANEL_CLOSED);
      panelHistory.clear();
    }
    else  // remove previous instance from history (should only apply to place info panel)
      panelHistory.erase(std::remove(panelHistory.begin(), panelHistory.end(), panel), panelHistory.end());
  }
  panel->setVisible(true);
  panelHistory.push_back(panel);
  showPanelContainer(true);
  panel->sdlUserEvent(gui, PANEL_OPENED);
}

bool MapsApp::popPanel()
{
  if(panelHistory.empty())
    return false;
  Widget* popped = panelHistory.back();
  panelHistory.pop_back();
  popped->sdlUserEvent(gui, PANEL_CLOSED);
  popped->setVisible(false);
  if(!panelHistory.empty())
    panelHistory.back()->setVisible(true);
  else
    showPanelContainer(false);
  return true;
}

// this should be a static method or standalone fn!
Toolbar* MapsApp::createPanelHeader(const SvgNode* icon, const char* title)
{
  Toolbar* toolbar = createToolbar();
  auto backBtn = createToolbutton(MapsApp::uiIcon("back"));
  backBtn->onClicked = [this](){ popPanel(); };
  toolbar->addWidget(backBtn);
  Widget* titleWidget = new Widget(widgetNode("#panel-header-title"));
  if(icon)
    static_cast<SvgUse*>(titleWidget->containerNode()->selectFirst(".icon"))->setTarget(icon);
  TextLabel* titleLabel = new TextLabel(titleWidget->containerNode()->selectFirst(".panel-title"));
  titleLabel->setText(title);
  //static_cast<SvgText*>(titleWidget->containerNode()->selectFirst("text"))->setText(title);
  toolbar->addWidget(titleWidget);

  //Widget* stretch = createStretch();
  if(panelSplitter) {
    titleLabel->addHandler([this](SvgGui* gui, SDL_Event* event) {
      if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
        panelSplitter->sdlEvent(gui, event);
        return true;
      }
      return false;
    });
  }

  //toolbar->addWidget(stretch);
  return toolbar;
}

Button* MapsApp::createPanelButton(const SvgNode* icon, const char* title, Widget* panel, bool menuitem)
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

  Button* btn = NULL;
  if(menuitem)
    btn = createMenuItem(title, icon);
  else {
    btn = new Button(proto->clone());
    btn->setIcon(icon);
    btn->setTitle(title);
    setupTooltip(btn, title);
  }

  panel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE)
      btn->setChecked(true);
    else if(event->type == MapsApp::PANEL_CLOSED)
      btn->setChecked(false);
    return false;  // continue to next handler
  });

  btn->onClicked = [=](){
    if(btn->isChecked() && !panelContainer->isVisible())
      showPanelContainer(true);
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
    minimizeBtn->node->addClass("minimize-btn");
    minimizeBtn->onClicked = [this](){
      // options:
      // 1. hide container and splitter and show a floating restore btn
      // 2. use setSplitSize() to shrink panel to toolbar height
      showPanelContainer(false);
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
#define NVG_LOG PLATFORM_LOG
#define USE_NVG_GL 1
#if USE_NVG_GL
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg-2/src/nanovg_vtex.h"
#include "nanovg-2/src/nanovg_gl_utils.h"
#endif
#define NANOVG_SW_IMPLEMENTATION
#define NVGSWU_GLES2
#define NVGSW_QUIET_FRAME  // suppress axis-aligned scissor warning
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
.legend text { fill: inherit; }
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

  <g id="panel-header-title" margin="0 3" layout="flex" flex-direction="row" box-anchor="hfill">
    <use class="panel-icon icon" width="36" height="36" xlink:href="" />
    <text class="panel-title" box-anchor="hfill" margin="0 9"></text>
  </g>
</svg>
)#";

static ThreadSafeQueue< std::function<void()> > taskQueue;
static std::thread::id mainThreadId;

void PLATFORM_WakeEventLoop() { glfwPostEmptyEvent(); }
void TANGRAM_WakeEventLoop() { glfwPostEmptyEvent(); }

void MapsApp::runOnMainThread(std::function<void()> fn)
{
  if(std::this_thread::get_id() == mainThreadId)
    fn();
  else {
    taskQueue.push_back(std::move(fn));
    glfwPostEmptyEvent();
  }
}

void glfwSDLEvent(SDL_Event* event)
{
  event->common.timestamp = SDL_GetTicks();
  if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_PRINTSCREEN)
    SvgGui::debugLayout = true;
  else if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F12)
    SvgGui::debugDirty = !SvgGui::debugDirty;
  else if(std::this_thread::get_id() != mainThreadId)
    MapsApp::runOnMainThread([_event = *event](){ glfwSDLEvent((SDL_Event*)&_event); });
  else
    MapsApp::gui->sdlEvent(event);
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

void MapsApp::initResources()
{
  Painter::loadFont("sans", FSPath(baseDir, "scenes/fonts/roboto-regular.ttf").c_str());
  if(Painter::loadFont("fallback", FSPath(baseDir, "scenes/fonts/DroidSansFallback.ttf").c_str()))
    Painter::addFallbackFont(NULL, "fallback");  // base font = NULL to set as global fallback

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
  styleSheet->parse_stylesheet(defaultColorsCSS);
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
  defs->addChild(uiIcon("chevron-down")->clone());
  defs->addChild(uiIcon("chevron-left")->clone());
  defs->addChild(uiIcon("chevron-right")->clone());
  widgetDoc->addChild(defs, widgetDoc->firstChild());

  setGuiResources(widgetDoc, styleSheet);
  gui = new SvgGui();
  gui->fullRedraw = USE_NVG_GL;  // see below
  // scaling
  gui->paintScale = 2.0;  //210.0/150.0;
  gui->inputScale = 1/gui->paintScale;
  nvgAtlasTextThreshold(Painter::sharedVg, 24 * gui->paintScale);  // 24px font is default for dialog titles

  // preset colors for tracks and bookmarks
  for(const auto& colorstr : config["colors"])
    markerColors.push_back(parseColor(colorstr.Scalar()));

  // DB setup
  FSPath dbPath(baseDir, "places.sqlite");
  if(sqlite3_open_v2(dbPath.c_str(), &bkmkDB, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
    logMsg("Error creating %s", dbPath.c_str());
    sqlite3_close(bkmkDB);
    bkmkDB = NULL;
  }
  else {
    if(sqlite3_create_function(bkmkDB, "osmSearchRank", 3, SQLITE_UTF8, 0, udf_osmSearchRank, 0, 0) != SQLITE_OK)
      LOGE("sqlite3_create_function: error creating osmSearchRank for places DB");
  }
}

int MapsApp::mainLoop(SDL_Window* sdlWindow, Platform* _platform)
{
  mainThreadId = std::this_thread::get_id();

  bool runApplication = true;

  int nvgFlags = NVG_AUTOW_DEFAULT;  // | (Painter::sRGB ? NVG_SRGB : 0);
  //int nvglFBFlags = NVG_IMAGE_SRGB;
#if USE_NVG_GL
  NVGcontext* nvgContext = nvglCreate(nvgFlags);
  //NVGLUframebuffer* nvglFB = nvgluCreateFramebuffer(nvgContext, 0, 0, NVGLU_NO_NVG_IMAGE | nvglFBFlags);
  //nvgluSetFramebufferSRGB(1);  // no-op for GLES - sRGB enabled iff FB is sRGB
#else
  NVGcontext* nvgContext = nvgswCreate(nvgFlags);
  NVGSWUblitter* swBlitter = nvgswuCreateBlitter();
  uint32_t* swFB = NULL;
#endif
  if(!nvgContext) { PLATFORM_LOG("Error creating nanovg context.\n"); return -1; }

  Painter::sharedVg = nvgContext;
  Painter* painter = new Painter(Painter::sharedVg);
  SvgPainter boundsPaint(painter);
  SvgDocument::sharedBoundsCalc = &boundsPaint;
  initResources();

  platform = _platform;
  Tangram::Map* tangramMap = new Tangram::Map(std::unique_ptr<Platform>(_platform));
  MapsApp* app = new MapsApp(tangramMap);
  app->map->setupGL();

  // fake location updates to test track recording
  auto locFn = [&](){
    real lat = app->currLocation.lat + 0.0001*(0.5 + std::rand()/real(RAND_MAX));
    real lng = app->currLocation.lng + 0.0001*(0.5 + std::rand()/real(RAND_MAX));
    real alt = app->currLocation.alt + 10*std::rand()/real(RAND_MAX);
    app->updateLocation(Location{mSecSinceEpoch()/1000.0, lat, lng, 0, alt, 0, 0, 0, 0, 0});
  };

  Timer* locTimer = NULL;
  Window* win = app->createGUI();
  win->sdlWindow = sdlWindow;
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
      app->pluginManager->reload(MapsApp::baseDir + "plugins");
      app->loadSceneFile();  // reload scene
      app->mapsSources->sceneVarsLoaded = false;
    }
    return false;
  });
  gui->showWindow(win, NULL);

  app->mapsOffline->resumeDownloads();

  if(sceneFile) {
    app->sceneFile = baseUrl.resolve(Url(sceneFile)).string();  //"import:\n  - " +
    app->loadSceneFile();
  }
  else
    app->mapsSources->rebuildSource(app->config["sources"]["last_source"].Scalar());

#if PLATFORM_DESKTOP
  // Alamo square
  app->updateLocation(Location{0, 37.777, -122.434, 0, 100, 0, 0, 0, 0, 0});
#endif

  while(runApplication) {
#if PLATFORM_DESKTOP
    app->needsRender() ? glfwPollEvents() : glfwWaitEvents();
#elif PLATFORM_ANDROID
    taskQueue.wait();
#else
#error "TODO"
#endif

    std::function<void()> queuedFn;
    while(taskQueue.pop_front(queuedFn))
      queuedFn();

    int fbWidth = 0, fbHeight = 0;
    SDL_GL_GetDrawableSize(sdlWindow, &fbWidth, &fbHeight);  //glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
    painter->deviceRect = Rect::wh(fbWidth, fbHeight);

    // We could consider drawing to offscreen framebuffer to allow limiting update to dirty region, but since
    //  we expect the vast majority of all frames drawn by app to be map changes (panning, zooming), the total
    //  benefit from partial update would be relatively small.  Furthermore, smooth map interaction is even more
    //  important than smooth UI interaction, so if map can't redraw at 60fps, we should focus on fixing that.

    // Just call Painter:endFrame() again to draw UI over map if UI isn't dirty
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

    // map rendering moved out of layoutAndDraw since object selection (which can trigger UI changes) occurs during render!
    if(platform->notifyRender()) {
      auto t0 = std::chrono::high_resolution_clock::now();
      double currTime = std::chrono::duration<double>(t0.time_since_epoch()).count();
      app->mapUpdate(currTime);
      //app->mapsWidget->node->setDirty(SvgNode::PIXELS_DIRTY);  -- so we can draw unchanged UI over map
      app->map->render();
      // selection queries are processed by render() - if nothing selected, tapLocation will still be valid
      if(!std::isnan(app->tapLocation.longitude)) {
        if(!app->panelHistory.empty() && app->panelHistory.back() == app->infoPanel)
          app->popPanel();
        app->mapsTracks->tapEvent(app->tapLocation);
        app->tapLocation = {NAN, NAN};
      }
    }
    else if(dirty.isValid())
      app->map->render();  // only have to rerender map, not update
    else
      continue;  // neither map nor UI is dirty

#if USE_NVG_GL
    //if(dirty != painter->deviceRect)
    //  nvgluSetScissor(int(dirty.left), fbHeight - int(dirty.bottom), int(dirty.width()), int(dirty.height()));
    Painter::vgInUse = true;  // to avoid error in case of repeated endFrame
    painter->endFrame();  // render UI over map
    //nvgluSetScissor(0, 0, 0, 0);  // disable scissor
#else
    if(dirty.isValid()) {
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
      painter->endFrame();
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    nvgswuBlit(swBlitter, swFB, fbWidth, fbHeight,
        int(dirty.left), int(dirty.top), int(dirty.width()), int(dirty.height()));
#endif
    SDL_GL_SwapWindow(sdlWindow);   //glfwSwapBuffers(glfwWin);
  }

#if PLATFORM_DESKTOP
  app->onSuspend();
#endif
  while(sqlite3_stmt* stmt = sqlite3_next_stmt(bkmkDB, NULL))
    LOGW("SQLite statement was not finalized: %s", sqlite3_sql(stmt));  //sqlite3_finalize(stmt);
  sqlite3_close(bkmkDB);
  gui->closeWindow(win);
  delete gui;
  delete painter;
  delete app->map;
  delete app;
#if USE_NVG_GL
  nvglDelete(nvgContext);
#else
  nvgswuDeleteBlitter(swBlitter);
  nvgswDelete(nvgContext);
#endif
  return 0;
}

int main(int argc, char* argv[])
{
#if PLATFORM_WIN
  SetProcessDPIAware();
  winLogToConsole = attachParentConsole();  // printing to old console is slow, but Powershell is fine
#endif

  // config
  MapsApp::baseDir = "./";  //"/home/mwhite/maps/";  //argc > 0 ? FSPath(argv[0]).parentPath() : ".";
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

  Url baseUrl("file:///");
  char pathBuffer[PATH_MAX] = {0};
  if (getcwd(pathBuffer, PATH_MAX) != nullptr) {
      baseUrl = baseUrl.resolve(Url(std::string(pathBuffer) + "/"));
  }

  if(!glfwInit()) { PLATFORM_LOG("glfwInit failed.\n"); return -1; }
#if USE_NVG_GL
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
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
  SvgLength::defaultDpi = std::max(mode->width, mode->height)/11.2;

  // library for native file dialogs - https://github.com/btzy/nativefiledialog-extended
  // alternatives:
  // - https://github.com/samhocevar/portable-file-dialogs (no GTK)
  // - https://sourceforge.net/projects/tinyfiledialogs (no GTK)
  // - https://github.com/Geequlim/NativeDialogs - last commit 2018
  NFD_Init();

  int res = MapsApp::mainLoop((SDL_Window*)glfwWin, new Tangram::LinuxPlatform());

  NFD_Quit();
  glfwTerminate();
  return res;
}
#endif

// rasterizing SVG markers (previous nanosvg impl removed 2023-08-13)

namespace Tangram {

bool userLoadSvg(char* svg, Texture* texture)
{
  std::unique_ptr<SvgDocument> doc(SvgParser().parseString(svg));
  if(!doc) return false;

  Painter boundsPaint(NULL);
  SvgPainter boundsCalc(&boundsPaint);
  doc->boundsCalculator = &boundsCalc;

  if(doc->hasClass("reflow-icons")) {
    real pad = doc->hasClass("reflow-icons-pad") ? 2 : 0;
    size_t nicons = doc->children().size();
    int nside = int(std::sqrt(nicons) + 0.5);
    int ii = 0;
    real rowheight = 0;
    SvgDocument* prev = NULL;
    for(SvgNode* child : doc->children()) {
      if(child->type() != SvgNode::DOC) continue;
      auto childdoc = static_cast<SvgDocument*>(child);
      if(prev) {
        childdoc->m_x = ii%nside ? prev->m_x + prev->width().px() + pad : 0;
        childdoc->m_y = ii%nside ? prev->m_y : prev->m_y + rowheight;
        //childdoc->invalidate(false); ... shouldn't be necessary
      }
      real h = childdoc->height().px() + pad;
      rowheight = ii%nside ? std::max(rowheight, h) : h;
      prev = childdoc;
      ++ii;
    }
    Rect b = doc->bounds();
    doc->setWidth(b.width());
    doc->setHeight(b.height());
  }

  int w = int(doc->width().px() + 0.5), h = int(doc->height().px() + 0.5);
  Image img(w, h);
  // this fn will be run on a thread if loading scene async, so we cannot use shared nvg context
  NVGcontext* drawCtx = nvgswCreate(NVG_AUTOW_DEFAULT | NVG_SRGB | NVGSW_PATHS_XC);
  {
    Painter painter(&img, drawCtx);
    painter.setBackgroundColor(::Color::INVALID_COLOR);
    painter.beginFrame();
    painter.translate(0, h);
    painter.scale(1, -1);
    SvgPainter(&painter).drawNode(doc.get());  //, dirty);
    painter.endFrame();
  }
  nvgswDelete(drawCtx);

  auto atlas = std::make_unique<SpriteAtlas>();
  bool hasSprites = false;
  for(auto pair : doc->m_namedNodes) {
    if(pair.second->type() != SvgNode::DOC) continue;
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
