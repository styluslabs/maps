#include "mapsapp.h"
#include "tangram.h"
#include "scene/scene.h"
#include "util.h"
#include "pugixml.hpp"
#include "rapidjson/document.h"
#include <sys/stat.h>
#include <fstream>
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
#include "resources.h"

//#define UTRACE_ENABLE
#define UTRACE_IMPLEMENTATION
#include "ulib/utrace.h"

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
SvgGui* MapsApp::gui = NULL;
bool MapsApp::runApplication = true;
ThreadSafeQueue< std::function<void()> > MapsApp::taskQueue;
std::thread::id MapsApp::mainThreadId;
static Tooltips tooltipsInst;

void MapsApp::runOnMainThread(std::function<void()> fn)
{
  if(std::this_thread::get_id() == mainThreadId)
    fn();
  else {
    taskQueue.push_back(std::move(fn));
    PLATFORM_WakeEventLoop();
  }
}

#define SVGGUI_UTIL_IMPLEMENTATION
#include "ugui/svggui_util.h"  // sdlEventLog for debugging

void MapsApp::sdlEvent(SDL_Event* event)
{
  //LOGW("%s", sdlEventLog(event).c_str());
  runOnMainThread([_event = *event]() mutable { gui->sdlEvent(&_event); });
}

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

  if(namestr.empty() && props.IsObject()) {
    if(props.HasMember("name_en"))
      namestr = props["name_en"].GetString();
    if(namestr.empty() && props.HasMember("name"))
      namestr = props["name"].GetString();
  }

  std::string osmid = osmIdFromProps(props);
  pickResultCoord = pos;
  pickResultName = namestr;
  if(&props != &pickResultProps)  // rapidjson asserts this
    pickResultProps.CopyFrom(props, pickResultProps.GetAllocator());
  currLocPlaceInfo = (locMarker > 0 && pickedMarkerId == locMarker);
  flyToPickResult = true;
  // allow pick result to be used as waypoint
  if(mapsTracks->onPickResult())
    return;

  // Need something to show for name, but we do not want to use this for pickNameResult
  std::string placetype = osmPlaceType(props);
  if(namestr.empty()) {
    if(!placetype.empty())
      namestr.swap(placetype);
    else
      namestr = fstring("%.6f, %.6f", pos.latitude, pos.longitude);
  }
  // show marker
  if(pickResultMarker == 0)
    pickResultMarker = map->markerAdd();
  map->markerSetVisible(pickResultMarker, !currLocPlaceInfo);

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

  Button* shareLocBtn = createToolbutton(MapsApp::uiIcon("share"), "Share");
  std::string geoquery = Url::escapeReservedCharacters(namestr);
  shareLocBtn->onClicked = [=](){ openURL(fstring("geo:%.7f,%.7f?q=%s", pos.latitude, pos.longitude, geoquery.c_str()).c_str()); };
  toolbar->addWidget(shareLocBtn);

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

  // show place type
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
    SvgDocument* svgDoc = SvgParser().parseString(icon);
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
        new TextLabel(wrapper);
        //if(!strchr(textbox->text().c_str(), '\n'))
        wrapper->setAttribute("box-anchor", "hfill");
        node = wrapper;
      }
    }
    else if(node->type() == SvgNode::TEXT) {
      SvgText* textnode = static_cast<SvgText*>(node);
      int textw = getPanelWidth() - (icon[0] ? 20 : 70);
      if(textnode->hasClass("wrap-text"))
        textnode->setText(SvgPainter::breakText(textnode, textw).c_str());
      else if(textnode->hasClass("elide-text"))
        SvgPainter::elideText(textnode, textw);
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
  map->handleDoubleTapGesture(x, y);
}

void MapsApp::clearPickResult()
{
  pickResultProps.SetNull();
  if(pickResultMarker > 0)
    map->markerSetVisible(pickResultMarker, false);
  pickResultCoord = LngLat(NAN, NAN);
  currLocPlaceInfo = false;
  flyToPickResult = false;
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
    if(!mapsTracks->activeTrack)
      showPanel(infoPanel, true);  // let's try not clearing history
    setPickResult(result->coordinates, "", props->toJson());
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

  //x *= density;
  //y *= density;
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
  // fallback fonts
  FSPath basePath(baseDir);
  for(auto& font : MapsApp::config["fallback_fonts"])
    options.fallbackFonts.push_back(Tangram::FontSourceHandle(Url(basePath.child(font.Scalar()).path)));
  // single worker much easier to debug (alternative is gdb scheduler-locking option)
  if(config["num_tile_workers"].IsScalar())
    options.numTileWorkers = atoi(config["num_tile_workers"].Scalar().c_str());
  map->loadScene(std::move(options), async);
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
  mapState = map->update(time - lastFrameTime);
  lastFrameTime = time;
  if(mapState.isAnimating())  // !mapState.viewComplete() - TileWorker requests rendering when new tiles ready
    platform->requestRender();
  //LOG("MapState: %X", mapState.flags);

  // update map center
  auto cpos = map->getCameraPosition();
  reorientBtn->setVisible(cpos.tilt != 0 || cpos.rotation != 0);
  reorientBtn->containerNode()->selectFirst(".icon")->setTransform(Transform2D::rotating(cpos.rotation));

  if(locMarker > 0 && pickedMarkerId == locMarker) {
    if(!mapsTracks->activeTrack)
      showPanel(infoPanel);
    setPickResult(currLocation.lngLat(), "Current location", "");
    mapsSearch->clearSearch();  // ???
    pickedMarkerId = 0;
  }

  sendMapEvent(MAP_CHANGE);
  pickedMarkerId = 0;  // prevent repeated searched for ignored marker
}

//void MapsApp::onResize(int wWidth, int wHeight, int fWidth, int fHeight)
//{
//  float new_density = (float)fWidth / (float)wWidth;
//  if (new_density != density) {
//    //recreate_context = true;
//    density = new_density;
//  }
//  map->setPixelScale(pixel_scale*density);
//  map->resize(fWidth, fHeight);  // this just calls setViewport
//}

void MapsApp::onSuspend()
{
  sendMapEvent(SUSPEND);
  saveConfig();
}

void MapsApp::updateLocMarker()
{
  if(!locMarker) {
    locMarker = map->markerAdd();
    map->markerSetStylingFromPath(locMarker, "layers.loc-marker.draw.marker");
    map->markerSetDrawOrder(locMarker, INT_MAX);
  }
  map->markerSetPoint(locMarker, currLocation.lngLat());
  map->markerSetProperties(locMarker, {{
      {"hasfix", hasLocation ? 1 : 0}, {"selected", currLocPlaceInfo ? 1 : 0}, {"angle", orientation}}});
}

void MapsApp::updateLocation(const Location& _loc)
{
  Point l0, l1;
  map->lngLatToScreenPosition(currLocation.lng, currLocation.lat, &l0.x, &l0.y);
  map->lngLatToScreenPosition(_loc.lng, _loc.lat, &l1.x, &l1.y);
  //LOGW("Location update dist: %.2f pixels", l1.dist(l0));

  currLocation = _loc;
  if(currLocation.time <= 0)
    currLocation.time = mSecSinceEpoch()/1000.0;
  updateLocMarker();

  if(currLocPlaceInfo) {
    SvgText* coordnode = static_cast<SvgText*>(infoContent->containerNode()->selectFirst(".lnglat-text"));
    std::string locstr = fstring("%.6f, %.6f", currLocation.lat, currLocation.lng);
    if(currLocation.poserr > 0)
      locstr += fstring(" (\u00B1%.0f m)", currLocation.poserr);
    // m/s -> kph or mph
    locstr += metricUnits ? fstring(" %.1f km/h", currLocation.spd*3.6)
        : fstring(" %.1f mph", currLocation.spd*2.23694);
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
    gpsStatusBtn->setText(fstring("%d", satsVisible).c_str());  //"%d/%d", satsUsed
  bool doupdate = (satsUsed > 0) != hasLocation;
  hasLocation = satsUsed > 0;
  if(doupdate) updateLocMarker();
}

void MapsApp::updateOrientation(float azimuth, float pitch, float roll)
{
  float deg = azimuth*180.0f/float(M_PI);
  if(std::abs(deg - orientation) < 1.0f) return;
  orientation = deg;
  //LOGW("orientation: %.1f deg", orientation);
  updateLocMarker();
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

MapsWidget::MapsWidget(MapsApp* _app) : Widget(new SvgCustomNode), app(_app)
{
  onApplyLayout = [this](const Rect& src, const Rect& dest){
    if(dest != viewport) {
      Map* map = app->map.get();
      Rect r = dest * (1/app->gui->inputScale);
      real y = window()->winBounds().height()/app->gui->inputScale - r.bottom;
      int w = int(r.width() + 0.5), h = int(r.height() + 0.5);
      LngLat pos;
      map->screenPositionToLngLat(w/2.0, h/2.0, &pos.longitude, &pos.latitude);
      map->setViewport(int(r.left + 0.5), int(y + 0.5), w, h);
      // by default, map center is preserved by resize, but we want upper left corner to be fixed instead
      // ... but skip on initial layout (detected by Rect viewport not yet set)
      if(viewport.isValid())
        map->setPosition(pos.longitude, pos.latitude);
      app->platform->requestRender();
    }
    if(src != dest)
      node->invalidate(true);
    viewport = dest;
    return true;
  };

  addHandler([this](SvgGui* gui, SDL_Event* event){
    // dividing by inputScale is a temporary hack - touchHandler should work in device independent coords (and
    //  why doesn't map's pixel scale apply to coords?)
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK)
      gui->setPressed(this);
    return app->touchHandler->sdlEvent(gui, event);
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
  void draw(SvgPainter* svgp) const override {}
  Rect bounds(SvgPainter* svgp) const override;
  void directDraw(Painter* p) const;

  Map* map;
};

Rect ScaleBarWidget::bounds(SvgPainter* svgp) const
{
  return svgp->p->getTransform().mapRect(Rect::wh(100, 14));
}

void ScaleBarWidget::directDraw(Painter* p) const
{
  //Painter* p = svgp->p;
  Rect bbox = node->bounds();
  p->save();
  p->translate(bbox.origin());
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
  p->setFontSize(12);
  p->setStroke(Color::WHITE, 2);
  p->drawText(0, 0, str.c_str());
  p->setFillBrush(Color::BLACK);
  p->setStroke(Color::NONE);
  p->drawText(0, 0, str.c_str());
  p->restore();
}

int MapsApp::getPanelWidth() const
{
  Widget* w = panelContainer->isVisible() ? panelContainer : mainTbContainer;
  return int(w->node->bounds().width() + 0.5);
}

void MapsApp::setWindowLayout(int fbWidth)
{
  bool narrow = fbWidth/gui->paintScale < 700;
  if(!currLayout || narrow != currLayout->node->hasClass("window-layout-narrow")) {
    if(currLayout) currLayout->setVisible(false);
    currLayout = win->selectFirst(narrow ? ".window-layout-narrow" : ".window-layout-wide");
    currLayout->setVisible(true);

    panelSplitter->setEnabled(narrow);
    panelSeparator = currLayout->selectFirst(".panel-separator");  // may be NULL
    panelContainer = currLayout->selectFirst(".panel-container");
    mainTbContainer = currLayout->selectFirst(".main-tb-container");

    mainToolbar->removeFromParent();
    mainTbContainer->addWidget(mainToolbar);

    panelContent->removeFromParent();
    panelContainer->addWidget(panelContent);

    mapsContent->removeFromParent();
    currLayout->selectFirst(".maps-container")->addWidget(mapsContent);

    // adjust menu alignment
    auto menubtns = mainToolbar->select(".toolbutton");
    for(Widget* btn : menubtns) {
      Menu* menu = static_cast<Button*>(btn)->mMenu;
      if(menu) {
        menu->setAlign(narrow ? (menu->mAlign | Menu::ABOVE) : (menu->mAlign & ~Menu::ABOVE));
        // first menu item always closest to opening button (anchor point)
        menu->selectFirst(".child-container")->node->setAttribute("flex-direction", narrow ? "column-reverse" : "column");
      }
    }

    SvgNode* minicon = MapsApp::uiIcon(narrow ? "chevron-down" : "chevron-up");
    auto minbtns = panelContent->select(".minimize-btn");
    for(Widget* btn : minbtns)
      static_cast<Button*>(btn)->setIcon(minicon);
  }
}

void MapsApp::createGUI(SDL_Window* sdlWin)
{
  static const char* mainWindowSVG = R"#(
    <svg class="window" layout="box">
      <g class="window-layout-narrow" display="none" box-anchor="fill" layout="flex" flex-direction="column">
        <rect class="statusbar-bg toolbar" display="none" box-anchor="hfill" x="0" y="0" width="20" height="30" />
        <g class="maps-container" box-anchor="fill" layout="box"></g>
        <rect class="panel-splitter background splitter" display="none" box-anchor="hfill" width="10" height="0"/>
        <g class="panel-container" display="none" box-anchor="hfill" layout="box">
          <rect class="background" box-anchor="fill" x="0" y="0" width="20" height="20" />
          <rect class="results-split-sizer" fill="none" box-anchor="hfill" width="320" height="200"/>
        </g>
        <g class="main-tb-container" box-anchor="hfill" layout="box"></g>
      </g>

      <g class="window-layout-wide" display="none" box-anchor="fill" layout="box">
        <g class="maps-container" box-anchor="fill" layout="box"></g>
        <g class="panel-layout" box-anchor="top left" margin="20 0 0 20" layout="flex" flex-direction="column">
          <g class="main-tb-container" box-anchor="hfill" layout="box">
            <rect class="background" fill="none" x="0" y="0" width="360" height="1"/>
          </g>
          <rect class="panel-separator" class="hrule title background" display="none" box-anchor="hfill" width="20" height="2"/>
          <g class="panel-container" display="none" box-anchor="hfill" layout="box">
            <rect class="background" box-anchor="hfill" x="0" y="0" width="20" height="800"/>
          </g>
        </g>
      </g>
    </svg>
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
  win.reset(new Window(winnode));
  win->sdlWindow = sdlWin;
  win->isFocusable = true;  // top level window should always be focusable

  panelContent = new Widget(loadSVGFragment("<g id='panel-content' box-anchor='fill' layout='box'></g>"));
  mapsContent = new Widget(loadSVGFragment("<g id='maps-content' box-anchor='fill' layout='box'></g>"));
  panelSplitter = new Splitter(winnode->selectFirst(".panel-splitter"),
          winnode->selectFirst(".results-split-sizer"), Splitter::BOTTOM, 200);
  panelSplitter->setSplitSize(config["ui"]["split_size"].as<int>(350));
  panelSplitter->onSplitChanged = [this](real size){
    if(size == panelSplitter->minSize) {
      showPanelContainer(false);  // minimize panel
      panelSplitter->setSplitSize(std::max(panelSplitter->initialSize, panelSplitter->minSize + 40));
    }
  };

  // adjust map center to account for sidebar
  //Tangram::EdgePadding padding = {0,0,0,0};  //{200, 0, 0, 0};
  //map->setPadding(padding);

  infoContent = new Widget(loadSVGFragment(R"#(<g layout="box" box-anchor="hfill"></g>)#"));
  auto infoHeader = createPanelHeader(NULL, "");  //MapsApp::uiIcon("pin"), "");
  infoPanel = createMapPanel(infoHeader, infoContent);
  infoPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == MapsApp::PANEL_CLOSED)
      clearPickResult();
    return false;
  });

  // toolbar w/ buttons for search, bookmarks, tracks, sources
  mainToolbar = createMenubar();  //createToolbar();
  mainToolbar->selectFirst(".child-container")->node->setAttribute("justify-content", "space-between");
  Button* tracksBtn = mapsTracks->createPanel();
  Button* searchBtn = mapsSearch->createPanel();
  Button* sourcesBtn = mapsSources->createPanel();
  Button* bkmkBtn = mapsBookmarks->createPanel();
  Button* pluginBtn = pluginManager->createPanel();

  //mainToolbar->autoClose = true;
  searchBtn->mMenu->autoClose = true;
  bkmkBtn->mMenu->autoClose = true;
  tracksBtn->mMenu->autoClose = true;
  sourcesBtn->mMenu->autoClose = true;

  mainToolbar->addButton(searchBtn);
  mainToolbar->addButton(bkmkBtn);
  mainToolbar->addButton(tracksBtn);
  mainToolbar->addButton(sourcesBtn);
  //mainToolbar->addButton(pluginBtn);

  Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
  Menu* overflowMenu = createMenu(Menu::VERT);
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

  Button* themeCb = createCheckBoxMenuItem("Use light theme");
  themeCb->onClicked = [=](){
    themeCb->setChecked(!themeCb->checked());
    themeCb->checked() ? win->node->addClass("light") : win->node->removeClass("light");
  };
  overflowMenu->addItem(themeCb);

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
  Button* offlineCb = createCheckBoxMenuItem("Offline");
  offlineCb->onClicked = [=](){
    offlineCb->setChecked(!offlineCb->isChecked());
    platform->isOffline = offlineCb->isChecked();
  };
  debugMenu->addItem(offlineCb);
  // dump contents of tile in middle of screen
  debugMenu->addItem("Dump tile", [this](){
    dumpTileContents(map->getViewportWidth()/2, map->getViewportHeight()/2);
  });
  overflowMenu->addSubmenu("Debug", debugMenu);

  mainToolbar->addButton(overflowBtn);

  // map widget and floating btns
  mapsWidget = new MapsWidget(this);
  mapsWidget->node->setAttribute("box-anchor", "fill");
  mapsWidget->isFocusable = true;
  mapsContent->addWidget(mapsWidget);

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
  recenterBtn->onClicked = [=](){
    if(!sensorsEnabled) {
      setSensorsEnabled(true);
      sensorsEnabled = true;
      recenterBtn->setIcon(MapsApp::uiIcon("gps-location"));
    }
    else
      map->flyTo(CameraPosition{currLocation.lng, currLocation.lat, map->getZoom()}, 1.0);
  };

  // should we forward motion events to map (so if user accidently starts drag on button it still works?)
  recenterBtn->addHandler([=](SvgGui* gui, SDL_Event* event){
    if(isLongPressOrRightClick(event)) {
      sensorsEnabled = !sensorsEnabled;
      setSensorsEnabled(sensorsEnabled);
      recenterBtn->setIcon(MapsApp::uiIcon(sensorsEnabled ? "gps-location" : "gps-location-off"));
      if(!sensorsEnabled)
        gpsStatusBtn->setVisible(false);
      // prevent click event; should OUTSIDE_PRESSED be sent by SvgGui before long press event?
      recenterBtn->sdlUserEvent(gui, SvgGui::OUTSIDE_PRESSED, 0, event, recenterBtn);
      gui->pressedWidget = NULL;
      return true;
    }
    return false;
  });

  gpsStatusBtn = new Widget(loadSVGFragment(gpsStatusSVG));
  gpsStatusBtn->setVisible(false);

  floatToolbar->addWidget(gpsStatusBtn);
  floatToolbar->addWidget(reorientBtn);
  floatToolbar->addWidget(recenterBtn);
  floatToolbar->node->setAttribute("box-anchor", "bottom right");
  floatToolbar->setMargins(0, 10, 10, 0);
  mapsContent->addWidget(floatToolbar);

  scaleBar = new ScaleBarWidget(map.get());
  scaleBar->node->setAttribute("box-anchor", "bottom left");
  scaleBar->setMargins(0, 0, 6, 10);
  mapsContent->addWidget(scaleBar);

  crossHair = new CrosshairWidget();
  crossHair->setVisible(false);
  mapsContent->addWidget(crossHair);

  legendContainer = createColumn();
  legendContainer->node->setAttribute("box-anchor", "hfill bottom");
  legendContainer->node->addClass("legend");
  legendContainer->setMargins(0, 0, 14, 0);  // shift above scale bar
  mapsContent->addWidget(legendContainer);

  // misc setup
  placeInfoProviderIdx = pluginManager->placeFns.size();

  gui->showWindow(win.get(), NULL);
}

void MapsApp::showPanelContainer(bool show)
{
  panelContainer->setVisible(show);
  if(panelSeparator)
    panelSeparator->setVisible(show);
  if(panelSplitter->isEnabled()) {
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
  popped->setVisible(false);
  panelHistory.pop_back();
  popped->sdlUserEvent(gui, PANEL_CLOSED);
  if(!panelHistory.empty())
    panelHistory.back()->setVisible(true);
  else
    showPanelContainer(false);
  return true;
}

void MapsApp::maximizePanel(bool maximize)
{
  if(currLayout->node->hasClass("window-layout-narrow") && !panelHistory.empty()) {
    currLayout->selectFirst(".maps-container")->setVisible(!maximize);
    currLayout->selectFirst(".statusbar-bg")->setVisible(maximize);
    panelContainer->node->setAttribute("box-anchor", maximize ? "fill" : "hfill");
    panelSplitter->setEnabled(!maximize);
    Widget* minbtn = panelHistory.back()->selectFirst(".minimize-btn");
    if(minbtn)
      minbtn->setVisible(!maximize);
    notifyStatusBarBG(maximize ?
        win->node->hasClass("light") : !readSceneValue("global.dark_base_map").as<bool>(false));
  }
}

// make this a static method or standalone fn?
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

  // forward press event not captured by toolbuttons to splitter
  toolbar->addHandler([this](SvgGui* gui, SDL_Event* event) {
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      panelSplitter->sdlEvent(gui, event);
      return true;
    }
    return false;
  });

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
    auto minimizeBtn = createToolbutton(MapsApp::uiIcon("chevron-down"));
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
  panelContent->addWidget(panel);
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

SvgNode* MapsApp::uiIcon(const char* id)
{
  SvgNode* res = SvgGui::useFile(":/ui-icons.svg")->namedNode(id);
  ASSERT(res && "UI icon missing!");
  return res;
}

#if PLATFORM_DESKTOP
bool MapsApp::openURL(const char* url)
{
#if PLATFORM_WIN
  HINSTANCE result = ShellExecute(0, 0, PLATFORM_STR(url), 0, 0, SW_SHOWNORMAL);
  // ShellExecute returns a value greater than 32 if successful
  return (int)result > 32;
//#elif PLATFORM_ANDROID
//  AndroidHelper::openUrl(url);
//  return true;
//#elif PLATFORM_IOS
//  if(!strchr(url, ':'))
//    iosOpenUrl((std::string("http://") + url).c_str());
//  else
//    iosOpenUrl(url);
//  return true;
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
#endif

// note that we need to saveConfig whenever app is paused on mobile, so easiest for MapsComponents to just
//  update config as soon as change is made (vs. us having to broadcast a signal on pause)
void MapsApp::saveConfig()
{
  config["storage"]["offline"] = storageOffline.load();
  config["storage"]["total"] = storageTotal.load();

  CameraPosition pos = map->getCameraPosition();
  config["view"]["lng"] = pos.longitude;
  config["view"]["lat"] = pos.latitude;
  config["view"]["zoom"] = pos.zoom;
  config["view"]["rotation"] = pos.rotation;
  config["view"]["tilt"] = pos.tilt;

  config["ui"]["split_size"] = int(panelSplitter->currSize);

  //std::string s = YAML::Dump(config);
  YAML::Emitter emitter;
  //emitter.SetStringFormat(YAML::DoubleQuoted);
  emitter << config;
  FileStream fs(configFile.c_str(), "wb");
  fs.write(emitter.c_str(), emitter.size());
}

void MapsApp::setDpi(float dpi)
{
  float ui_scale = config["ui"]["ui_scale"].as<float>(1.0f);
  gui->paintScale = ui_scale*dpi/150.0;
  gui->inputScale = 1/gui->paintScale;
  SvgLength::defaultDpi = ui_scale*dpi;
  // Map takes coords in raw pixels (i.e. pixelScale doesn't apply to input coords)
  touchHandler->xyScale = float(gui->paintScale);
  map->setPixelScale(config["ui"]["map_scale"].as<float>(1.0f) * dpi/150.0f);
}

MapsApp::MapsApp(Platform* _platform) : touchHandler(new TouchHandler(this))
{
  TRACE_INIT();
  platform = _platform;
  mainThreadId = std::this_thread::get_id();
  metricUnits = config["metric_units"].as<bool>(true);
  // Google Maps and Apple Maps use opposite scaling for this gesture, so definitely needs to be configurable
  touchHandler->dblTapDragScale = config["gestures"]["dbl_tap_drag_scale"].as<float>(1.0f);

  initResources(baseDir.c_str());

  gui = new SvgGui();
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

  // make sure cache folder exists
  mkdir(FSPath(baseDir, "cache").c_str(), 0777);
  mkdir(FSPath(baseDir, "tracks").c_str(), 0777);

  // cache management
  storageTotal = config["storage"]["total"].as<int64_t>(0);
  storageOffline = config["storage"]["offline"].as<int64_t>(0);
  int64_t storageShrinkMax = config["storage"]["shrink_at"].as<int64_t>(500) * 1024*1024;
  int64_t storageShrinkMin = config["storage"]["shrink_to"].as<int64_t>(250) * 1024*1024;
  // easier to track total storage and offline map storage instead cached storage directly, since offline
  //   map download/deletion can convert some tiles between cached and offline
  platform->onNotifyStorage = [=](int64_t dtotal, int64_t doffline){
    storageTotal += dtotal;
    storageOffline += doffline;
    // write out changes to offline storage total immediately since errors here can persist; errors w/ total
    //  storage will be fixed by shrinkCache
    if(doffline)
      saveConfig();
    if(storageShrinkMax > 0 && storageTotal - storageOffline > storageShrinkMax && !mapsOffline->numOfflinePending()) {
      MapsOffline::queueOfflineTask(0, [=](){
        int64_t tot = MapsOffline::shrinkCache(storageShrinkMin);
        storageTotal = tot + storageOffline;  // update storage usage
      });
    }
  };

  map.reset(new Tangram::Map(std::unique_ptr<Platform>(_platform)));
  // Scene::onReady() remains false until after first call to Map::update()!
  //map->setSceneReadyListener([this](Tangram::SceneID id, const Tangram::SceneError*) {});
  //map->setCameraAnimationListener([this](bool finished){ sendMapEvent(CAMERA_EASE_DONE); });
  map->setPickRadius(2.0f);

  // Setup UI panels
  mapsSources = std::make_unique<MapsSources>(this);
  mapsOffline = std::make_unique<MapsOffline>(this);
  pluginManager = std::make_unique<PluginManager>(this, baseDir + "plugins");
  // no longer recreated when scene loaded
  mapsTracks = std::make_unique<MapsTracks>(this);
  mapsSearch = std::make_unique<MapsSearch>(this);
  mapsBookmarks = std::make_unique<MapsBookmarks>(this);

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
  while(sqlite3_stmt* stmt = sqlite3_next_stmt(bkmkDB, NULL))
    LOGW("SQLite statement was not finalized: %s", sqlite3_sql(stmt));  //sqlite3_finalize(stmt);
  sqlite3_close(bkmkDB);
  bkmkDB = NULL;

  gui->closeWindow(win.get());
  delete gui;  gui = NULL;
}

bool MapsApp::drawFrame(int fbWidth, int fbHeight)
{
  static uint64_t t0 = 0;
  TRACE_END(t0, "wait events");

  std::function<void()> queuedFn;
  while(taskQueue.pop_front(queuedFn))
    queuedFn();

  if(!runApplication) return false;

  if(glNeedsInit) {
    glNeedsInit = false;
    map->setupGL();
    // Painter created here since GL context required to build shaders
    painter.reset(new Painter(Painter::PAINT_GL | Painter::CACHE_IMAGES));
    scaleBarPainter.reset(new Painter(Painter::PAINT_GL));
    gui->fullRedraw = painter->usesGPU();
    painter->setAtlasTextThreshold(24 * gui->paintScale);  // 24px font is default for dialog titles
  }

  setWindowLayout(fbWidth);

  // We could consider drawing to offscreen framebuffer to allow limiting update to dirty region, but since
  //  we expect the vast majority of all frames drawn by app to be map changes (panning, zooming), the total
  //  benefit from partial update would be relatively small.  Furthermore, smooth map interaction is even more
  //  important than smooth UI interaction, so if map can't redraw at 60fps, we should focus on fixing that.
  // ... but as a simple optimization, we call Painter:endFrame() again to draw UI over map if UI isn't dirty
  painter->deviceRect = Rect::wh(fbWidth, fbHeight);
  Rect dirty = gui->layoutAndDraw(painter.get());
  TRACE_END(t0, "layoutAndDraw");

  if(flyToPickResult) {
    // ensure marker is visible and hasn't been covered by opening panel
    Point scr;
    auto campos = map->getCameraPosition();
    campos.longitude = pickResultCoord.longitude;
    campos.latitude = pickResultCoord.latitude;
    campos.zoom = std::min(campos.zoom, 16.0f);
    if(!map->lngLatToScreenPosition(campos.longitude, campos.latitude, &scr.x, &scr.y)
         || panelContainer->node->bounds().contains(scr/gui->paintScale)) {
      // if point is close enough, use simple ease instead of flyTo
      Point offset = scr - Point(fbWidth, fbHeight)/2;
      if(std::abs(offset.x) < 2*fbWidth && std::abs(offset.y) < 2*fbHeight)
        map->setCameraPositionEased(campos, 1.0);
      else
        map->flyTo(campos, 1.0);
    }
    flyToPickResult = false;
  }

  // map rendering moved out of layoutAndDraw since object selection (which can trigger UI changes) occurs during render!
  if(platform->notifyRender()) {
    auto now = std::chrono::high_resolution_clock::now();
    double currTime = std::chrono::duration<double>(now.time_since_epoch()).count();
    mapUpdate(currTime);
    TRACE_END(t0, "map update");
    //mapsWidget->node->setDirty(SvgNode::PIXELS_DIRTY);  -- so we can draw unchanged UI over map
    map->render();
    // selection queries are processed by render() - if nothing selected, tapLocation will still be valid
    if(!std::isnan(tapLocation.longitude)) {
      if(!panelHistory.empty() && panelHistory.back() == infoPanel)
        popPanel();
      mapsTracks->tapEvent(tapLocation);
      tapLocation = {NAN, NAN};
    }
  }
  else if(dirty.isValid())
    map->render();  // only have to rerender map, not update
  else
    return false;  // neither map nor UI is dirty

  TRACE_END(t0, "map render");
  // scale bar must be updated whenever map changes, but we don't want to redraw entire UI every frame
  // - a possible alternative is to draw with Tangram using something similar to DebugTextStyle/DebugStyle
  scaleBarPainter->deviceRect = Rect::wh(fbWidth, fbHeight);
  scaleBarPainter->beginFrame();
  scaleBarPainter->setsRGBAdjAlpha(true);
  scaleBarPainter->scale(gui->paintScale);  // beginFrame resets Painter state
  scaleBar->directDraw(scaleBarPainter.get());
  scaleBarPainter->endFrame();

  painter->endFrame();  // render UI over map
#if 0  // nanovg_sw renderer
  if(dirty.isValid()) {
    // clear dirty rect to transparent pixels
    painter->setCompOp(Painter::CompOp_Src);
    painter->fillRect(dirty, Color::TRANSPARENT_COLOR);
    painter->setCompOp(Painter::CompOp_SrcOver);
  }
  painter->endFrame();

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);
  painter->blitImageToScreen(dirty);
#endif
  TRACE_END(t0, fstring("UI render %d x %d", fbWidth, fbHeight).c_str());
  TRACE_FLUSH();
  return true;
}
