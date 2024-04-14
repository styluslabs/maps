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

MapsApp* MapsApp::inst = NULL;
Platform* MapsApp::platform = NULL;
std::string MapsApp::baseDir;
YAML::Node MapsApp::config;
std::string MapsApp::configFile;
bool MapsApp::metricUnits = true;
sqlite3* MapsApp::bkmkDB = NULL;
std::vector<Color> MapsApp::markerColors;
SvgGui* MapsApp::gui = NULL;
bool MapsApp::runApplication = true;
bool MapsApp::simulateTouch = false;
ThreadSafeQueue< std::function<void()> > MapsApp::taskQueue;
std::thread::id MapsApp::mainThreadId;
static Tooltips tooltipsInst;

struct JSCallInfo { int ncalls = 0; double secs = 0; };
static std::vector<JSCallInfo> jsCallStats;
static std::mutex jsStatsMutex;

namespace Tangram {
void reportJSTrace(uint32_t _id, double secs)
{
  std::lock_guard<std::mutex> lock(jsStatsMutex);
  if(jsCallStats.size() <= _id)
    jsCallStats.resize(_id+1);
  jsCallStats[_id].ncalls += 1;
  jsCallStats[_id].secs += secs;
}
}

static void dumpJSStats(Tangram::Scene* scene)
{
  std::lock_guard<std::mutex> lock(jsStatsMutex);
  if(scene) {
    auto& fns = scene->functions();
    for(size_t ii = 0; ii < fns.size() && ii < jsCallStats.size(); ++ii) {
      if(jsCallStats[ii].ncalls == 0) continue;
      std::string snip = fns[ii].substr(0, 150);
      std::replace(snip.begin(), snip.end(), '\n', ' ');
      Tangram::logMsg("JS: %10.3f us (%5d calls) for %s\n", jsCallStats[ii].secs*1E6, jsCallStats[ii].ncalls, snip.c_str());
    }
  }
  jsCallStats.clear();
}

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

// this should get list of name properties from config or current scene to support other languages
std::string MapsApp::getPlaceTitle(const Properties& props) const
{
  std::string name;
  props.getString("name_en", name) || props.getString("name", name);
  return name;
}

void MapsApp::setPickResult(LngLat pos, std::string namestr, const std::string& propstr)  //, int priority)
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
      <rect class="separator" margin="0 6" box-anchor="hfill" width="20" height="2"/>
      <g class="action-container" layout="box" box-anchor="hfill" margin="0 3"></g>
      <g class="waypt-section" layout="box" box-anchor="hfill"></g>
      <g class="bkmk-section" layout="box" box-anchor="hfill"></g>
      <g class="info-section" layout="flex" flex-direction="column" box-anchor="hfill"></g>
    </g>
  )#";

  static std::unique_ptr<SvgNode> placeInfoProto;
  if(!placeInfoProto)
    placeInfoProto.reset(loadSVGFragment(placeInfoProtoSVG));

  rapidjson::Document json;
  json.Parse(propstr.c_str());
  Properties props = jsonToProps(json);

  std::string placetype = !propstr.empty() ? pluginManager->jsCallFn("getPlaceType", propstr) : "";
  if(namestr.empty()) namestr = getPlaceTitle(props);
  if(namestr.empty()) namestr.swap(placetype);  // we can show type instead of name if present

  std::string osmid = osmIdFromJson(json);
  pickResultCoord = pos;
  pickResultName = namestr;
  pickResultProps = propstr;
  pickResultOsmId = osmid;
  bool wasCurrLoc = currLocPlaceInfo;
  currLocPlaceInfo = (locMarker > 0 && pickedMarkerId == locMarker);
  flyToPickResult = true;
  if(wasCurrLoc != currLocPlaceInfo)
    updateLocMarker();
  // allow pick result to be used as waypoint
  if(mapsTracks->onPickResult())
    return;
  // leave pickResultName empty if no real name
  if(namestr.empty()) namestr = lngLatToStr(pos);

  // show marker
  if(pickResultMarker == 0)
    pickResultMarker = map->markerAdd();
  map->markerSetVisible(pickResultMarker, !currLocPlaceInfo);

  // show place info panel
  gui->deleteContents(infoContent);  //, ".listitem");
  Widget* item = new Widget(placeInfoProto->clone());
  infoContent->addWidget(item);

  showPanel(infoPanel, true);
  Widget* minbtn = infoPanel->selectFirst(".minimize-btn");
  if(minbtn) minbtn->setVisible(panelHistory.size() > 1);

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

  real titlewidth = getPanelWidth() - 60;  // must be done before setText!
  TextLabel* titlelabel = static_cast<TextLabel*>(infoPanel->selectFirst(".panel-title"));
  titlelabel->setText(namestr.c_str());
  titlelabel->setText(SvgPainter::breakText(static_cast<SvgText*>(titlelabel->node), titlewidth).c_str());

  Widget* bkmkSection = mapsBookmarks->getPlaceInfoSection(osmid, pos);
  if(bkmkSection) {
    if(!bkmkSection->containerNode()->children().empty())
      item->selectFirst(".bkmk-section")->addWidget(createHRule(2, "0 6"));
    item->selectFirst(".bkmk-section")->addWidget(bkmkSection);
  }

  if(currLocPlaceInfo) {
    item->selectFirst(".place-info-row")->setVisible(false);
    item->selectFirst(".currloc-info-row")->setVisible(true);
    updateLocation(currLocation);  // set lng, lat, elevation text
    return;
  }

  Widget* distwdgt = item->selectFirst(".dist-text");
  Widget* diricon = item->selectFirst(".direction-icon");
  if(distwdgt && diricon) {
    distwdgt->setVisible(hasLocation);
    diricon->setVisible(hasLocation);
    double dist = lngLatDist(currLocation.lngLat(), pos);
    double bearing = lngLatBearing(currLocation.lngLat(), pos);
    SvgUse* icon = static_cast<SvgUse*>(diricon->node);
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
      elevWidget->setText(elevToStr(elev).c_str());
      infoContent->selectFirst(".elevation-icon")->setVisible(true);  //elevWidget->setVisible(true);
    }
  };

  if(json.IsObject() && json.HasMember("ele"))
    elevFn(json["ele"].GetDouble());
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
    infoContent->selectFirst(".info-section")->addWidget(createHRule(2, "0 6"));
    infoContent->selectFirst(".info-section")->addWidget(providerRow);
    //infoContent->selectFirst(".info-section")->addWidget(createTitledRow("Information from ", providerSel));
    providerSel->onChanged("");
  }

  if(json.IsObject() && json.HasMember("place_info")) {
    for(auto& info : json["place_info"].GetArray())
      addPlaceInfo(info["icon"].GetString(), info["title"].GetString(), info["value"].GetString());
  }

  // must be last (or we must copy props)
  props.set("name", namestr);
  map->markerSetStylingFromPath(pickResultMarker, "layers.pick-marker.draw.marker");
  map->markerSetPoint(pickResultMarker, pos);  // geometry must be set before properties for new marker!
  map->markerSetProperties(pickResultMarker, std::move(props));  //{"priority", priority}
}

void MapsApp::placeInfoPluginError(const char* err)
{
  Button* retryBtn = createToolbutton(MapsApp::uiIcon("retry"), "Retry", true);
  retryBtn->onClicked = [=](){
    pluginManager->jsPlaceInfo(placeInfoProviderIdx - 1, pickResultOsmId);
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
          <use class="icon" width="30" height="30" xlink:href=""/>
        </g>
        <g class="value-container" box-anchor="hfill" layout="box" margin="0 10"></g>
      </g>
      <rect class="listitem-separator separator" margin="0 2 0 2" box-anchor="bottom hfill" width="20" height="1"/>
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
    else if(node->type() == SvgNode::IMAGE) {
      // this will be revisited when we have multiple images for display
      auto* imgnode = static_cast<SvgImage*>(node);
      Button* b = new Button(node);
      b->onClicked = [imgnode](){
        MapsApp::openURL(imgnode->m_linkStr.c_str());
      };
      imgnode->setSize(Rect::wh(getPanelWidth() - 20, 0));
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

void MapsApp::fingerEvent(int action, float x, float y)
{
  LngLat pos;
  map->screenPositionToLngLat(x, y, &pos.longitude, &pos.latitude);

  mapsTracks->fingerEvent(action, pos);
}

void MapsApp::clearPickResult()
{
  pickResultName.clear();
  pickResultProps.clear();
  pickResultOsmId.clear();
  pickResultCoord = LngLat(NAN, NAN);
  flyToPickResult = false;
  if(pickResultMarker > 0)
    map->markerSetVisible(pickResultMarker, false);
  if(currLocPlaceInfo) {
    currLocPlaceInfo = false;
    updateLocMarker();
  }
}

void MapsApp::tapEvent(float x, float y)
{
  //LngLat location;
  map->screenPositionToLngLat(x, y, &tapLocation.longitude, &tapLocation.latitude);
#if 0  //IS_DEBUG
  double xx, yy;
  map->lngLatToScreenPosition(tapLocation.longitude, tapLocation.latitude, &xx, &yy);
  LOGD("tapEvent: %f,%f -> %f,%f (%f, %f)\n", x, y, tapLocation.longitude, tapLocation.latitude, xx, yy);
#endif

  map->pickLabelAt(x, y, [this](const Tangram::LabelPickResult* result) {
    auto& props = result->touchItem.properties;
    LOGD("Picked label: %s", result ? props->getAsString("name").c_str() : "none");
    if(!result) return;
    std::string itemId = props->getAsString("id");
    std::string osmType = props->getAsString("osm_type");
    if(itemId.empty())
      itemId = props->getAsString("osm_id");
    if(osmType.empty())
      osmType = "node";
    // we'll clear history iff panel is minimized
    if(!panelContainer->isVisible())  // !mapsTracks->activeTrack)
      showPanel(infoPanel, false);
    setPickResult(result->coordinates, "", props->toJson());
    tapLocation = {NAN, NAN};
  });

  map->pickMarkerAt(x, y, [this](const Tangram::MarkerPickResult* result) {
    if(!result) return;
    LOGD("Marker %d picked", result->id);
    if(result->id == pickResultMarker) {
      if(panelContainer->isVisible())
        return;  // info panel will be closed and pick result cleared
      showPanelContainer(true);
    }
    else {
      //map->markerSetVisible(pickResultMarker, false);  // ???
      pickedMarkerId = result->id;
      map->screenPositionToLngLat(
          result->position[0], result->position[1], &pickResultCoord.longitude, &pickResultCoord.latitude);
      //pickResultCoord = result->coordinates;  -- just set to center of marker for polylines/polygons
    }
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
  options.updates.push_back(SceneUpdate{"global.metric_units", metricUnits ? "true" : "false"});
  options.updates.push_back(SceneUpdate{"global.shuffle_seed", std::to_string(shuffleSeed)});
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
  dumpJSStats(NULL);  // reset stats
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

static bool camerasMatch(const CameraPosition& cam0, const CameraPosition& cam1)
{
  float drot = std::abs(cam0.rotation - cam1.rotation)*180/M_PI;
  drot = std::min(drot, 360 - drot);
  return std::abs(cam0.zoom - cam1.zoom) < 1E-7
      && std::abs(cam0.longitude - cam1.longitude) < 1E-7
      && std::abs(cam0.latitude - cam1.latitude) < 1E-7
      && drot < 1.0;
}

void MapsApp::mapUpdate(double time)
{
  static double lastFrameTime = 0;

  if(followState == FOLLOW_ACTIVE && !camerasMatch(map->getCameraPosition(), prevCamPos)) {
    followState = NO_FOLLOW;
    prevCamPos = {};
    recenterBtn->setIcon(MapsApp::uiIcon("gps-location"));
  }
  if(locMarkerAngle != orientation + map->getRotation()*180/float(M_PI))
    updateLocMarker();

  mapState = map->update(time - lastFrameTime);
  lastFrameTime = time;
  //LOG("MapState: %X", mapState.flags);
  if(mapState.isAnimating())  // !mapState.viewComplete() - TileWorker requests rendering when new tiles ready
    platform->requestRender();
  else if(followState == FOLLOW_PENDING)
    followState = FOLLOW_ACTIVE;

  // update map center
  auto cpos = map->getCameraPosition();
  reorientBtn->setVisible(cpos.tilt != 0 || cpos.rotation != 0);
  reorientBtn->containerNode()->selectFirst(".icon")->setTransform(Transform2D::rotating(cpos.rotation));

  sendMapEvent(MAP_CHANGE);
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

// Map::flyTo() zooms out and then back in, inappropriate for short flights
void MapsApp::gotoCameraPos(const CameraPosition& campos)
{
  int w = map->getViewportHeight(), h = map->getViewportHeight();
  Point scr;
  map->lngLatToScreenPosition(campos.longitude, campos.latitude, &scr.x, &scr.y);
  // if point is close enough, use simple ease instead of flyTo
  Point offset = scr - Point(w, h)/2;
  if(std::abs(offset.x) < 2*w && std::abs(offset.y) < 2*h)
    map->setCameraPositionEased(campos, std::max(0.2, std::min(offset.dist()/200, 1.0)));
  else
    map->flyTo(campos, 1.0);
}

void MapsApp::updateLocMarker()
{
  if(!locMarker) {
    locMarker = map->markerAdd();
    map->markerSetStylingFromPath(locMarker, "layers.loc-marker.draw.marker");
    map->markerSetDrawOrder(locMarker, INT_MAX);
  }
  locMarkerAngle = orientation + map->getRotation()*180/float(M_PI);
  map->markerSetPoint(locMarker, currLocation.lngLat());
  map->markerSetProperties(locMarker, {{ {"hasfix", hasLocation ? 1 : 0},
      {"selected", currLocPlaceInfo ? 1 : 0}, {"angle", locMarkerAngle}}});
}

void MapsApp::updateLocation(const Location& _loc)
{
  //Point l0, l1;
  //map->lngLatToScreenPosition(currLocation.lng, currLocation.lat, &l0.x, &l0.y);
  //map->lngLatToScreenPosition(_loc.lng, _loc.lat, &l1.x, &l1.y);
  //LOGW("Location update dist: %.2f pixels", l1.dist(l0));

  currLocation = _loc;
  if(currLocation.time <= 0)
    currLocation.time = mSecSinceEpoch()/1000.0;
  updateLocMarker();

  if(followState == FOLLOW_ACTIVE) {
    prevCamPos.longitude = _loc.lng;
    prevCamPos.latitude = _loc.lat;
    map->setCameraPosition(prevCamPos);
  }

  if(currLocPlaceInfo) {
    SvgText* coordnode = static_cast<SvgText*>(infoContent->containerNode()->selectFirst(".lnglat-text"));
    std::string locstr = lngLatToStr(currLocation.lngLat());
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
      elevnode->setText(elevToStr(elev).c_str());
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
  deg = deg - 360*std::floor(deg/360);
  if(std::abs(deg - orientation) < (followState == FOLLOW_ACTIVE ? 0.1f : 1.0f)) return;
  orientation = deg;
  // we might have to add a low-pass for this
  if(followState == FOLLOW_ACTIVE) {
    prevCamPos.rotation = -deg*float(M_PI)/180;
    map->setCameraPosition(prevCamPos);
  }
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
  double x0 = ox*tex->width() - 0.5, y0 = oy*tex->height() - 0.5;  // -0.5 to adjust for pixel centers
  // we should extrapolate at edges instead of clamping - see shader in raster_contour.yaml
  int ix0 = std::max(0, int(std::floor(x0))), iy0 = std::max(0, int(std::floor(y0)));
  int ix1 = std::min(int(std::ceil(x0)), tex->width()-1), iy1 = std::min(int(std::ceil(y0)), tex->height()-1);
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

std::string MapsApp::elevToStr(double meters)
{
  return fstring(metricUnits ? "%.0f m" : "%.0f ft", metricUnits ? meters : meters*3.28084);
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
  //Widget* w = panelContainer->isVisible() ? panelContainer : mainTbContainer;
  return int(mainTbContainer->node->bounds().width() + 0.5);
}

void MapsApp::populateColorPickerMenu()
{
  static const char* menuSVG = R"#(
    <g class="menu" display="none" position="absolute" box-anchor="fill" layout="box">
      <rect box-anchor="fill" width="20" height="20"/>
      <g class="child-container" box-anchor="fill"
          layout="flex" flex-direction="row" flex-wrap="wrap" justify-content="flex-start" margin="6 6">
      </g>
    </g>
  )#";

  if(!colorPickerMenu) {
    colorPickerMenu = new SharedMenu(loadSVGFragment(menuSVG), Menu::VERT_LEFT);
    win->addWidget(colorPickerMenu);
  }
  else
    gui->deleteContents(colorPickerMenu->selectFirst(".child-container"));

  for(size_t ii = 0; ii < std::min(size_t(15), markerColors.size()); ++ii) {
    Color color = markerColors[ii];
    Button* btn = new Button(widgetNode("#colorbutton"));
    btn->selectFirst(".btn-color")->node->setAttr<color_t>("fill", color.color);
    if(ii > 0 && ii % 4 == 0)
      btn->node->setAttribute("flex-break", "before");
    btn->onClicked = [=](){ static_cast<ColorPicker*>(colorPickerMenu->host)->updateColor(color.color); };
    colorPickerMenu->addItem(btn);
  }

  Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"));
  overflowBtn->onClicked = [this](){
    auto* colorbtn = static_cast<ColorPicker*>(colorPickerMenu->host);
    customColorDialog->setColor(colorbtn->color());
    customColorDialog->onColorAccepted =
        [=](Color c){ static_cast<ColorPicker*>(colorPickerMenu->host)->updateColor(c); };
    showModalCentered(customColorDialog.get(), gui);
  };
  colorPickerMenu->addItem(overflowBtn);
}

void MapsApp::setWindowLayout(int fbWidth)
{
  bool narrow = fbWidth/gui->paintScale < 700;
  if(currLayout && narrow == currLayout->node->hasClass("window-layout-narrow")) return;

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
  // let's try hiding panel title icons if we also show the main toolbar w/ the same icon
  auto panelicons = panelContent->select(".panel-icon");
  for(Widget* btn : panelicons)
    btn->setVisible(narrow);
}

void MapsApp::createGUI(SDL_Window* sdlWin)
{
  static const char* mainWindowSVG = R"#(
    <svg class="window" layout="box">
      <g class="window-layout-narrow" display="none" box-anchor="fill" layout="flex" flex-direction="column">
        <rect class="statusbar-bg toolbar" display="none" box-anchor="hfill" x="0" y="0" width="20" height="30" />
        <g class="maps-container" box-anchor="fill" layout="box"></g>
        <rect class="panel-splitter background splitter" display="none" box-anchor="hfill" width="10" height="0"/>
        <g class="panel-container panel-container-narrow" display="none" box-anchor="hfill" layout="box">
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
          <rect class="panel-separator hrule title background" display="none" box-anchor="hfill" width="20" height="2"/>
          <g class="panel-container panel-container-wide" display="none" box-anchor="hfill" layout="box">
            <rect class="background" box-anchor="hfill" x="0" y="0" width="20" height="800"/>
          </g>
        </g>
      </g>
    </svg>
  )#";

  static const char* gpsStatusSVG = R"#(
    <g class="gps-status-button toolbutton roundbutton" layout="box" box-anchor="hfill">
      <rect class="background" box-anchor="hfill" width="36" height="22" rx="5" ry="5"/>
      <g layout="flex" flex-direction="row" box-anchor="fill">
        <g class="image-container" margin="1 2">
          <use class="icon" width="18" height="18" xlink:href=":/ui-icons.svg#satellite"/>
        </g>
        <text class="title" margin="0 4"></text>
      </g>
    </g>
  )#";

  static const char* reorientSVG = R"#(
    <g class="reorient-btn toolbutton roundbutton" layout="box">
      <circle class="background" cx="21" cy="21" r="21"/>
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

  panelContent = createBoxLayout();

  Pager* panelPager = new Pager(winnode->selectFirst(".panel-container-narrow"));
  panelPager->getNextPage = [=](bool left) {
    for(size_t ii = 0; ii < panelPages.size(); ++ii) {
      if(panelPages[ii] == panelHistory.front()) {
        if(left ? ii > 0 : ii < panelPages.size() - 1) {
          panelPager->currPage = panelHistory.back();  //panelPages[ii];
          panelPager->nextPage = panelPages[left ? (ii-1) : (ii+1)];
        }
        return;
      }
    }
  };
  panelPager->onPageChanged = [this](Widget* page){
    showPanel(page);
  };

  mapsContent = new Widget(loadSVGFragment("<g id='maps-content' box-anchor='fill' layout='box'></g>"));
  panelSplitter = new Splitter(winnode->selectFirst(".panel-splitter"),
          winnode->selectFirst(".results-split-sizer"), Splitter::BOTTOM, 200);
  panelSplitter->setSplitSize(config["ui"]["split_size"].as<int>(350));
  panelSplitter->onSplitChanged = [this](real size){
    if(size == panelSplitter->minSize) {
      if(panelHistory.back()->node->hasClass("can-minimize"))
        showPanelContainer(false);  // minimize panel
      else
        while(!panelHistory.empty()) popPanel();
      panelSplitter->setSplitSize(std::max(panelSplitter->initialSize, panelSplitter->minSize + 40));
    }
  };
  // enable swipe down gesture to hide panel
  panelSplitter->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SDL_FINGERUP || event->type == SvgGui::OUTSIDE_PRESSED) {
      if(gui->flingV.y > 1000 && std::abs(gui->flingV.x) < 500)   // flingV is in pixels/sec
        panelSplitter->onSplitChanged(panelSplitter->minSize);  // dismiss panel
      return true;
    }
    return false;
  });

  // adjust map center to account for sidebar
  //map->setPadding({200, 0, 0, 0});

  customColorDialog.reset(new ManageColorsDialog(markerColors));
  customColorDialog->onColorListChanged = [this](){
    populateColorPickerMenu();
    config["colors"] = YAML::Node(YAML::NodeType::Sequence);
    for(Color& color : markerColors)
      config["colors"].push_back( colorToStr(color) );
  };
  populateColorPickerMenu();

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
  Button* searchBtn = mapsSearch->createPanel();
  Button* bkmkBtn = mapsBookmarks->createPanel();
  Button* tracksBtn = mapsTracks->createPanel();
  Button* sourcesBtn = mapsSources->createPanel();
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
    setWindowXmlClass(themeCb->checked() ? "light" : "");
  };
  overflowMenu->addItem(themeCb);

  Menu* debugMenu = createMenu(Menu::HORZ);
  const char* debugFlags[9] = {"Freeze tiles", "Proxy colors", "Tile bounds",
      "Tile info", "Label bounds", "Tangram info", "Draw all labels", "Tangram stats", "Selection buffer"};
  for(int ii = 0; ii < 9; ++ii) {
    Button* debugCb = createCheckBoxMenuItem(debugFlags[ii]);
    debugCb->onClicked = [=](){
      debugCb->setChecked(!debugCb->isChecked());
      setDebugFlag(Tangram::DebugFlags(ii), debugCb->isChecked());
      //loadSceneFile();  -- most debug flags shouldn't require scene reload
    };
    debugMenu->addItem(debugCb);
  }
  overflowMenu->addSubmenu("Tangram debug", debugMenu);

  // fake location updates to test track recording
  auto fakeLocFn = [this](){
    real lat = currLocation.lat + 0.0001*(0.5 + std::rand()/real(RAND_MAX));
    real lng = currLocation.lng + 0.0001*(0.5 + std::rand()/real(RAND_MAX));
    real alt = currLocation.alt + 10*std::rand()/real(RAND_MAX);
    updateLocation(Location{mSecSinceEpoch()/1000.0, lat, lng, 0, alt, 0, 0, 0, 0, 0});
  };

  Menu* appDebugMenu = createMenu(Menu::HORZ);
#if PLATFORM_DESKTOP
  Button* simTouchCb = createCheckBoxMenuItem("Simulate touch");
  simTouchCb->onClicked = [=](){
    simTouchCb->setChecked(!simTouchCb->isChecked());
    MapsApp::simulateTouch = simTouchCb->isChecked();
  };
  appDebugMenu->addItem(simTouchCb);
#endif
  Button* offlineCb = createCheckBoxMenuItem("Offline");
  offlineCb->onClicked = [=](){
    offlineCb->setChecked(!offlineCb->isChecked());
    platform->isOffline = offlineCb->isChecked();
  };
  appDebugMenu->addItem(offlineCb);

  Timer* fakeLocTimer = NULL;
  Button* fakeLocCb = createCheckBoxMenuItem("Simulate motion");
  fakeLocCb->onClicked = [=]() mutable {
    fakeLocCb->setChecked(!fakeLocCb->isChecked());
    if(fakeLocTimer) {
      gui->removeTimer(fakeLocTimer);
      fakeLocTimer = NULL;
    }
    if(fakeLocCb->isChecked())
      fakeLocTimer = gui->setTimer(2000, win.get(), [&](){ MapsApp::runOnMainThread(fakeLocFn); return 2000; });
  };
  appDebugMenu->addItem(fakeLocCb);

  // dump contents of tile in middle of screen
  appDebugMenu->addItem("Dump tile", [this](){
    dumpTileContents(map->getViewportWidth()/2, map->getViewportHeight()/2);
  });
  appDebugMenu->addItem("Print JS stats", [this](){ dumpJSStats(map->getScene()); });
  appDebugMenu->addItem("Set location", [this](){
    if(!std::isnan(pickResultCoord.latitude)) {
      currLocation.lng = pickResultCoord.longitude;
      currLocation.lat = pickResultCoord.latitude;
    }
    else
      map->getPosition(currLocation.lng, currLocation.lat);
    updateLocation(currLocation);
  });
  overflowMenu->addSubmenu("App debug", appDebugMenu);

  mainToolbar->addButton(overflowBtn);

  // map widget and floating btns
  mapsWidget = new MapsWidget(this);
  mapsWidget->node->setAttribute("box-anchor", "fill");
  mapsWidget->isFocusable = true;
  mapsContent->addWidget(mapsWidget);

  // recenter, reorient btns
  //Toolbar* floatToolbar = createVertToolbar();
  Widget* floatToolbar = createColumn();
  // we could switch to different orientation modes (travel direction, compass direction) w/ multiple taps
  reorientBtn = new Button(loadSVGFragment(reorientSVG));  //createToolbutton(MapsApp::uiIcon("compass"), "Reorient");
  reorientBtn->setMargins(0, 0, 6, 0);
  reorientBtn->onClicked = [this](){
    prevCamPos = map->getCameraPosition();
    prevCamPos.tilt = 0;
    prevCamPos.rotation = 0;
    map->setCameraPositionEased(prevCamPos, 1.0);
    if(followState != NO_FOLLOW) {
      followState = NO_FOLLOW;
      recenterBtn->setIcon(MapsApp::uiIcon("gps-location"));
    }
  };
  reorientBtn->setVisible(false);

  //recenterBtn = createToolbutton(MapsApp::uiIcon("gps-location"), "Recenter");
  recenterBtn = new Button(widgetNode("#roundbutton"));
  recenterBtn->setIcon(MapsApp::uiIcon("gps-location"));
  recenterBtn->onClicked = [=](){
    if(!sensorsEnabled) {
      setSensorsEnabled(true);
      sensorsEnabled = true;
      recenterBtn->setIcon(MapsApp::uiIcon("gps-location"));
      return;
    }
    if(followState != NO_FOLLOW)
      return;
    auto campos = map->getCameraPosition();
    bool cammatch = camerasMatch(campos, prevCamPos);
    //prevCamPos = {};
    campos.longitude = currLocation.lng;
    campos.latitude = currLocation.lat;
    //campos.zoom = std::min(campos.zoom, 16.0f);
    Point loc, center(map->getViewportWidth()/2, map->getViewportHeight()/2);
    bool locvisible = map->lngLatToScreenPosition(currLocation.lng, currLocation.lat, &loc.x, &loc.y);
    if(cammatch && center.dist(loc)/gui->paintScale < 40) {
      campos.zoom = std::min(20.0f, campos.zoom + 1);
      map->setCameraPositionEased(campos, 0.35f);
      prevCamPos = campos;
    }
    else if(!locvisible && !std::isnan(pickResultCoord.latitude) &&
        map->lngLatToScreenPosition(pickResultCoord.longitude, pickResultCoord.latitude, NULL, NULL)) {
      auto viewboth = map->getEnclosingCameraPosition(pickResultCoord, currLocation.lngLat(), {32});
      campos.zoom = viewboth.zoom - 1;
      gotoCameraPos(campos);  //, 1.0);
      if(campos.zoom >= 12)
        prevCamPos = campos;
    }
    else {
      if(campos.zoom < 12) campos.zoom = 15;
      gotoCameraPos(campos);  //, 1.0);
      prevCamPos = campos;
    }
  };

  // should we forward motion events to map (so if user accidently starts drag on button it still works?)
  recenterBtn->addHandler([=](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERUP && gui->fingerClicks == 2) {
      prevCamPos = map->getCameraPosition();
      bool follow = followState == NO_FOLLOW;
      followState = follow ? FOLLOW_PENDING : NO_FOLLOW;
      prevCamPos.longitude = currLocation.lng;
      prevCamPos.latitude = currLocation.lat;
      prevCamPos.rotation = follow ? -orientation*float(M_PI)/180 : 0;
      map->setCameraPositionEased(prevCamPos, 1.0f);
      recenterBtn->setIcon(MapsApp::uiIcon(follow ? "nav-arrow" : "gps-location"));
    }
    else if(isLongPressOrRightClick(event)) {
      sensorsEnabled = !sensorsEnabled;
      setSensorsEnabled(sensorsEnabled);
      recenterBtn->setIcon(MapsApp::uiIcon(sensorsEnabled ? "gps-location" : "gps-location-off"));
      hasLocation = false;  // if enabling, still need to wait for GPS status update
      if(!sensorsEnabled) {
        gpsStatusBtn->setVisible(false);
        updateLocMarker();
      }
    }
    else
      return false;
    // prevent click event; should OUTSIDE_PRESSED be sent by SvgGui before long press event?
    recenterBtn->sdlUserEvent(gui, SvgGui::OUTSIDE_PRESSED, 0, event, recenterBtn);
    gui->pressedWidget = NULL;
    return true;
  });

  gpsStatusBtn = new Widget(loadSVGFragment(gpsStatusSVG));
  gpsStatusBtn->setMargins(0, 0, 6, 0);
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
  if(!panelHistory.empty() && !show)
    panelHistory.back()->setVisible(false);  // to send INVISIBLE event to panel
  panelContainer->setVisible(show);
  if(panelSeparator)
    panelSeparator->setVisible(show);
  if(panelSplitter->isEnabled()) {
    panelSplitter->setVisible(show);
    mainTbContainer->setVisible(!show);
  }
  if(!panelHistory.empty() && show)
    panelHistory.back()->setVisible(true);  // to send VISIBLE event to panel
}

void MapsApp::showPanel(Widget* panel, bool isSubPanel)
{
  if(!panelHistory.empty()) {
    if(panelHistory.back() == panel) {
      panel->setVisible(true);
      showPanelContainer(true);
      return;
    }
    if(panelHistory.size() > 1 && panelHistory[panelHistory.size() - 2] == panel) {
      popPanel();
      return;
    }
    panelHistory.back()->setVisible(false);
    if(!isSubPanel) {
      for(Widget* w : panelHistory)
        w->sdlUserEvent(gui, PANEL_CLOSED);
      panelHistory.clear();
      panelToSkip = NULL;
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

  if(panelHistory.empty())
    showPanelContainer(false);
  else if(panelHistory.back() == panelToSkip) {
    // hack to handle the few cases where we want to go directly to a subpanel (e.g. directions)
    panelToSkip = NULL;
    popPanel();
  }
  else
    panelHistory.back()->setVisible(true);

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
    currLayout->selectFirst(".statusbar-bg")->setVisible(PLATFORM_MOBILE && maximize);
    panelContainer->node->setAttribute("box-anchor", maximize ? "fill" : "hfill");
    panelSplitter->setEnabled(!maximize);
    notifyStatusBarBG(maximize ?
        win->node->hasClass("light") : !readSceneValue("global.dark_base_map").as<bool>(false));
  }
  Widget* minbtn = panelHistory.back()->selectFirst(".minimize-btn");
  if(minbtn)
    minbtn->setVisible(!maximize);
}

// make this a static method or standalone fn?
Toolbar* MapsApp::createPanelHeader(const SvgNode* icon, const char* title)
{
  Toolbar* toolbar = createToolbar();
  auto backBtn = createToolbutton(MapsApp::uiIcon("back"));
  backBtn->onClicked = [this](){ popPanel(); };
  toolbar->addWidget(backBtn);
  Widget* titleWidget = new Widget(widgetNode("#panel-header-title"));
  // need widget to show/hide icon in setWindowLayout()
  Widget* iconWidget = new Widget(titleWidget->containerNode()->selectFirst(".panel-icon"));
  if(icon)
    static_cast<SvgUse*>(iconWidget->node)->setTarget(icon);
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
    panelPages.push_back(panel);
  }

  panel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == MapsApp::PANEL_OPENED)
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
  // on mobile, easier to just swipe down to minimize instead of tapping button
  if(PLATFORM_DESKTOP && canMinimize) {
    auto minimizeBtn = createToolbutton(MapsApp::uiIcon("chevron-down"));
    minimizeBtn->node->addClass("minimize-btn");
    minimizeBtn->onClicked = [this](){ showPanelContainer(false); };
    header->addWidget(minimizeBtn);
  }

  Widget* panel = createColumn();
  if(canMinimize) panel->node->addClass("can-minimize");
  panel->node->setAttribute("box-anchor", "fill");
  panel->addWidget(header);
  panel->addWidget(createHRule());
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
  inst = this;
  platform = _platform;
  mainThreadId = std::this_thread::get_id();
  metricUnits = config["metric_units"].as<bool>(true);
  // Google Maps and Apple Maps use opposite scaling for this gesture, so definitely needs to be configurable
  touchHandler->dblTapDragScale = config["gestures"]["dbl_tap_drag_scale"].as<float>(1.0f);
  shuffleSeed = config["random_shuffle_seed"].as<bool>(true) ? std::rand() : 0;

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
    painter.reset(new Painter(Painter::PAINT_GL | Painter::CACHE_IMAGES));  //Painter::PAINT_SW | Painter::SW_BLIT_GL
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
    bool onscr = map->lngLatToScreenPosition(campos.longitude, campos.latitude, &scr.x, &scr.y);
    if(!onscr || panelContainer->node->bounds().contains(scr/gui->paintScale))
      gotoCameraPos(campos);
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
    // selection queries are processed by render(); if nothing selected, tapLocation will still be valid
    if(pickedMarkerId > 0) {
      if(pickedMarkerId == locMarker) {
        if(!mapsTracks->activeTrack)
          showPanel(infoPanel);
        setPickResult(currLocation.lngLat(), "Current location", "");
      }
      else
        sendMapEvent(MARKER_PICKED);
      pickedMarkerId = 0;
    }
    else if(!std::isnan(tapLocation.longitude)) {
      if(!panelHistory.empty() && panelHistory.back() == infoPanel)
        popPanel();  // closing info panel will clear pick result
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
  if(crossHair->isVisible())
    crossHair->directDraw(scaleBarPainter.get());
  scaleBarPainter->endFrame();

  if(painter->usesGPU())
    painter->endFrame();  // render UI over map
  else {  // nanovg_sw renderer
    if(dirty.isValid()) {
      painter->setBackgroundColor(Color::INVALID_COLOR);
      painter->targetImage->fillRect(dirty, Color::TRANSPARENT_COLOR);
      painter->endFrame();
    }
    painter->blitImageToScreen(dirty, true);
  }
  TRACE_END(t0, fstring("UI render %d x %d", fbWidth, fbHeight).c_str());
  TRACE_FLUSH();
  return true;
}
