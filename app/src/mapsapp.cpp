#include "mapsapp.h"
#include "tangram.h"
#include "scene/scene.h"
#include "debug/textDisplay.h"
#include "util.h"
#include "pugixml.hpp"
#include <sys/stat.h>
#include <fstream>
// for elevation
#include "util/imageLoader.h"
#include "util/elevationManager.h"
#include "debug/frameInfo.h"

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
#include "nanovg_gl_utils.h"
#include "nanovg_sw_utils.h"
#include "mapwidgets.h"
#include "resources.h"
#if PLATFORM_IOS
#include "../ios/iosApp.h"
#elif PLATFORM_ANDROID
#include "../android/tangram/src/main/cpp/sqlite_fdvfs.h"
#endif

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
bool MapsApp::terrain3D = true;
sqlite3* MapsApp::bkmkDB = NULL;
std::vector<Color> MapsApp::markerColors;
SvgGui* MapsApp::gui = NULL;
bool MapsApp::runApplication = true;
bool MapsApp::simulateTouch = false;
bool MapsApp::lowPowerMode = false;
ThreadSafeQueue< std::function<void()> > MapsApp::taskQueue;
std::thread::id MapsApp::mainThreadId;
static Tooltips tooltipsInst;

static constexpr int versionCode = 2;
int MapsApp::prevVersion = 0;

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
    Tangram::logMsg("*** JS stats at %s\n", ftimestr("%FT%H:%M:%S").c_str());
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

      real pxscale = app->gui->paintScale;
      Rect r = window()->winBounds()*pxscale;
      int w = int(r.width() + 0.5), h = int(r.height() + 0.5);
      if(w != map->getViewportWidth() || h != map->getViewportHeight())
        map->setViewport(0, 0, w, h);

      int desth = int(dest.height()*pxscale + 0.5);
      map->setPadding(Tangram::EdgePadding(0, 0, 0, h - desth, false));

      auto margins = app->currLayout->node->hasClass("window-layout-narrow") ?
            glm::vec4(app->topInset, 0, (h - desth)/pxscale + 10, 0) :
            glm::vec4(0, 0, 0, app->getPanelWidth()+20);  // TRBL
      Tangram::TextDisplay::Instance().setMargins(margins);
      app->platform->requestRender();
    }
    if(src != dest)
      node->invalidate(true);
    viewport = dest;
    return true;
  };
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
  void draw(SvgPainter* svgp) const override { if(!useDirectDraw) directDraw(svgp->p); }
  Rect bounds(SvgPainter* svgp) const override;
  void directDraw(Painter* p) const;

  Map* map;
  bool useDirectDraw = false;
};

Rect ScaleBarWidget::bounds(SvgPainter* svgp) const
{
  return svgp->p->getTransform().mapRect(Rect::wh(100, 24));
}

void ScaleBarWidget::directDraw(Painter* p) const
{
  //Painter* p = svgp->p;
  Rect bbox = node->bounds();
  p->save();
  if(useDirectDraw)
    p->translate(bbox.origin());
  p->translate(2, 0);

  real y = bbox.center().y;
  LngLat r0, r1;
  map->screenPositionToLngLat(bbox.left*MapsApp::gui->paintScale, y, &r0.longitude, &r0.latitude);
  map->screenPositionToLngLat(bbox.right*MapsApp::gui->paintScale, y, &r1.longitude, &r1.latitude);

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

  real y0 = bbox.height() - 4;
  real w = bbox.width() - 4;
  p->setFillBrush(Color::NONE);
  p->setStroke(Color::WHITE, 4, Painter::RoundCap);
  p->drawLine(Point(0, y0), Point(w*scaledist/dist, y0));
  p->setStroke(Color::BLACK, 2, Painter::RoundCap);
  p->drawLine(Point(0, y0), Point(w*scaledist/dist, y0));
  // render text
  p->setFontSize(12);
  p->setStroke(Color::WHITE, 2);
  p->setStrokeAlign(Painter::StrokeOuter);
  p->setFillBrush(Color::BLACK);
  p->drawText(0, y0-6, str.c_str());
  p->restore();
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
  if(std::this_thread::get_id() == mainThreadId)
    gui->sdlEvent(event);
  else
    runOnMainThread([_event = *event]() mutable { gui->sdlEvent(&_event); });
}

void MapsApp::getMapBounds(LngLat& lngLatMin, LngLat& lngLatMax)
{
  Rect bounds = getMapViewport();
  int vieww = int(bounds.width() + 0.5), viewh = int(bounds.height() + 0.5);
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

Rect MapsApp::getMapViewport()
{
  return mapsWidget->node->bounds().toSize()*gui->paintScale;
}

Point MapsApp::lngLatToScreenPoint(LngLat lngLat)
{
  Point p;
  map->lngLatToScreenPosition(lngLat.longitude, lngLat.latitude, &p.x, &p.y);
  return p;  //* (1/gui->paintScale);
}

LngLat MapsApp::getMapCenter()
{
  LngLat res;
  // map viewport may extend outside visible area (i.e. MapsWidget) to account for, e.g, rounded corners
  Point center = getMapViewport().center();
  map->screenPositionToLngLat(center.x, center.y, &res.longitude, &res.latitude);
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
      <g class="action-container" layout="box" box-anchor="hfill" margin="3 0"></g>
      <g class="waypt-section" display="none" layout="flex" flex-direction="column" box-anchor="hfill">
        <rect class="separator section-hrule" margin="0 0" box-anchor="hfill" width="20" height="3.5"/>
      </g>
      <g class="bkmk-section" display="none" layout="flex" flex-direction="column" box-anchor="hfill">
        <rect class="separator section-hrule" margin="0 0" box-anchor="hfill" width="20" height="3.5"/>
      </g>
      <g class="info-section" display="none" layout="flex" flex-direction="column" box-anchor="hfill">
        <rect class="separator section-hrule" margin="0 0" box-anchor="hfill" width="20" height="3.5"/>
      </g>
    </g>
  )#";

  static std::unique_ptr<SvgNode> placeInfoProto;
  if(!placeInfoProto)
    placeInfoProto.reset(loadSVGFragment(placeInfoProtoSVG));

  YAML::Node json = strToJson(propstr.c_str());
  Properties props = jsonToProps(json);

  std::string placetype = !propstr.empty() ? pluginManager->jsCallFn("getPlaceType", propstr) : "";
  if(namestr.empty()) namestr = getPlaceTitle(props);
  if(namestr.empty()) namestr.swap(placetype);  // we can show type instead of name if present
  if(namestr.empty()) {
    for(auto& prop : props.items()) {
      if(prop.key.find("name") != std::string::npos) {
        namestr = props.getString(prop.key);
        break;
      }
    }
  }

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
  //Widget* minbtn = infoPanel->selectFirst(".minimize-btn");
  //if(minbtn) minbtn->setVisible(panelHistory.size() > 1);

  // actions toolbar
  Toolbar* toolbar = createToolbar();
  mapsBookmarks->addPlaceActions(toolbar);
  mapsTracks->addPlaceActions(toolbar);
  if(currLocPlaceInfo) {
    Button* followBtn = createActionbutton(MapsApp::uiIcon("nav-arrow"), "Follow");
    followBtn->onClicked = [=](){ toggleFollow(); };  //followBtn->setChecked(followState != NO_FOLLOW); };
    //followBtn->setChecked(followState != NO_FOLLOW);
    toolbar->addWidget(followBtn);
  }
  toolbar->addWidget(createStretch());

  Button* shareLocBtn = createActionbutton(MapsApp::uiIcon("share"), "Share");
  std::string geoquery = Url::escapeReservedCharacters(namestr);
  shareLocBtn->onClicked = [=](){ openURL(fstring("geo:%.7f,%.7f?q=%s", pos.latitude, pos.longitude, geoquery.c_str()).c_str()); };
  toolbar->addWidget(shareLocBtn);
  toolbar->node->addClass("action-bar");

  item->selectFirst(".action-container")->addWidget(toolbar);

  real titlewidth = getPanelWidth() - 60;  // must be done before setText!
  TextLabel* titlelabel = static_cast<TextLabel*>(infoPanel->selectFirst(".panel-title"));
  titlelabel->setText(namestr.c_str());
  titlelabel->setText(SvgPainter::breakText(static_cast<SvgText*>(titlelabel->node), titlewidth).c_str());

  mapsBookmarks->setPlaceInfoSection(osmid, pos);

  if(currLocPlaceInfo) {
    item->selectFirst(".place-info-row")->setVisible(false);
    item->selectFirst(".currloc-info-row")->setVisible(true);
    updateLocPlaceInfo();
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
    diricon->setVisible(dist < MapProjection::EARTH_CIRCUMFERENCE_METERS/1000/4);
    distwdgt->setText(distKmToStr(dist, 2, 4).c_str());
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

  if(const auto& ele = json["ele"])
    elevFn(ele.as<double>());
  else
    getElevation(pos, elevFn);

  Widget* infoSection = infoContent->selectFirst(".info-section");
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
    Widget* providerRow = createRow({createTextBox("Information from "), createStretch(), providerSel}, "3 6");
    infoSection->addWidget(providerRow);
    infoSection->setVisible(true);
    providerSel->onChanged("");
  }

  if(const auto& infos = json["place_info"]) {
    infoSection->setVisible(true);
    for(const auto& info : infos)
      addPlaceInfo(info["icon"].getCStr(), info["title"].getCStr(), info["value"].getCStr());
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
    gui->deleteContents(infoContent->selectFirst(".info-section"), ".listitem");
    pluginManager->jsPlaceInfo(placeInfoProviderIdx - 1, pickResultOsmId);
    retryBtn->setVisible(false);
  };
  infoContent->selectFirst(".info-section")->addWidget(retryBtn);
}

void MapsApp::addPlaceInfo(const char* icon, const char* title, const char* value)
{
  static const char* rowProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <g class="button-wrapper" layout="box" box-anchor="fill">
        <rect box-anchor="hfill" fill="none" width="48" height="42"/>
        <rect box-anchor="fill" width="48" height="48"/>
        <g class="child-container" layout="flex" flex-direction="row" box-anchor="hfill">
          <g class="image-container" margin="2 5">
            <use class="icon weak" width="26" height="26" xlink:href=""/>
          </g>
          <g class="value-container" box-anchor="hfill" layout="box" margin="0 10"></g>
        </g>
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
        // if only one link, make entire row clickable
        Button* b = new Button(anchorNodes.size() > 1 ? a : row->containerNode()->selectFirst(".button-wrapper"));
        b->onClicked = [a](){
          MapsApp::openURL(a->getStringAttr("href", a->getStringAttr("xlink:href")));
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
#if PLATFORM_IOS
      std::string imgkey = randomStr(16);
      imgnode->setXmlId(imgkey.c_str());
      iosPlatform_getPhotoData(imgnode->m_linkStr.c_str(), [=](const void* data, size_t len, float angle){
        Image image = Image::decodeBuffer(data, len);
        Image* pimg = new Image(std::move(image));  // std::function must be copyable
        MapsApp::runOnMainThread([=]() mutable {
          SvgNode* keynode = infoContent->containerNode()->selectFirst(("#" + imgkey).c_str());
          if(!pimg || keynode != imgnode) return;
          if(angle != 0) {
            auto tf = Transform2D::rotating(std::abs(angle)*M_PI/180.0);
            if(angle < 0) tf.scale(-1, 0);
            imgnode->setTransform(tf);
          }
          imgnode->m_image = std::move(*pimg);
          imgnode->invalidate(false);  // redraw
          delete pimg;  pimg = NULL;
        });
      });
#endif
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
  textnode->setText(SvgPainter::breakText(textnode, getPanelWidth() - 70).c_str());
  content->addWidget(new TextBox(textnode));
  if(split) {
    // collapsible section
    Widget* row2 = new Widget(rowProto->clone());
    Widget* content2 = new Widget(row2->containerNode()->selectFirst(".value-container"));
    content2->addWidget(new TextBox(createTextNode(split+1)));
    content2->node->setAttribute("font-size", "13");  // slightly smaller font

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
  if(!map->screenPositionToLngLat(x, y, &lng, &lat))
    return;

  // print tile bounds (e.g., for aligned OSM extracts)
  if(getDebugFlag(Tangram::tile_bounds)) {
    auto meters = MapProjection::lngLatToProjectedMeters({lng, lat});
    TileID tile = MapProjection::projectedMetersTile(meters, int(map->getZoom()));
    auto bounds = MapProjection::tileBounds(tile);
    auto llmin = MapProjection::projectedMetersToLngLat(bounds.min);
    auto llmax = MapProjection::projectedMetersToLngLat(bounds.max);
    LOG("%s: [%.9f,%.9f,%.9f,%.9f]", tile.toString().c_str(),
        llmin.longitude, llmin.latitude, llmax.longitude, llmax.latitude);
  }

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
    if(!result) return;
    auto& props = result->touchItem.properties;
    LOGD("Picked label: %s", result ? props->getAsString("name").c_str() : "none");
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

  map->pickFeatureAt(x, y, [this](const Tangram::FeaturePickResult* result) {
    if(!result) return;
    mapsTracks->onFeaturePicked(result);
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
  options.diskTileCacheMaxAge = config["storage"]["max_age"].as<int64_t>(options.diskTileCacheMaxAge);
  options.preserveMarkers = true;
  options.debugStyles = Tangram::getDebugFlag(Tangram::DebugFlags::tile_bounds);
  options.metricUnits = metricUnits;
  options.terrain3d = terrain3D;
  options.elevationSource = config["sources"]["elevation"][0].as<std::string>("");
  // fallback fonts
  FSPath basePath(baseDir);
  for(const auto& font : config["fallback_fonts"])
    options.fallbackFonts.push_back(Tangram::FontSourceHandle(Url(basePath.child(font.Scalar()).path)));
  // single worker much easier to debug (alternative is gdb scheduler-locking option)
  options.numTileWorkers = config["num_tile_workers"].as<int>(2);
  dumpJSStats(NULL);  // reset stats
  map->loadScene(std::move(options), async);

  // max tile cache size
  map->getScene()->tileManager()->setCacheSize(config["tile_cache_limit"].as<size_t>(512*1024*1024));
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

  if(locMarkerNeedsUpdate || locMarkerAngle != orientation + map->getRotation()*180/float(M_PI)) {
    updateLocMarker();
    platform->notifyRender();  // clear requestRender() from marker update
    locMarkerNeedsUpdate = false;
  }

  bool wasReady = map->getScene()->isReady();
  mapState = map->update(time - lastFrameTime);
  lastFrameTime = time;
  //LOG("MapState: %X", mapState.flags);
  if(mapState.isAnimating())  // !mapState.viewComplete() - TileWorker requests rendering when new tiles ready
    platform->requestRender();
  else {
    flyingToCurrLoc = false;
    if(followState == FOLLOW_PENDING)
      followState = FOLLOW_ACTIVE;
  }

  // it is possible to miss the pending_completion state due to async scene load
  if(!wasReady && map->getScene()->isReady()) {  //use_count() < 2
    tracksDataSource->rasterSources().clear();
    if(terrain3D) {
      //map->getScene()->elevationManager()->m_elevationSource
      auto elevsrc = getElevationSource();
      if(elevsrc)
        tracksDataSource->addRasterSource(elevsrc);
    }
    map->getScene()->tileManager()->addClientTileSource(tracksDataSource);
    platform->requestRender();
    //map->addTileSource(tracksDataSource);  -- Map will cache source and add to scenes automatically, which
    // we don't want until we've added elevation source
  }

  // update map center
  auto cpos = map->getCameraPosition();
  reorientBtn->setVisible(cpos.tilt != 0 || cpos.rotation != 0);
  SvgNode* iconNode = reorientBtn->containerNode()->selectFirst(".icon");
  auto tf = Transform2D::rotating(cpos.rotation);
  if(tf != iconNode->getTransform())
    iconNode->setTransform(tf);

  // update progress indicator (shown if still loading tiles after 1 sec)
  const auto& tileMgr = map->getScene()->tileManager();
  currProgress = 1 - tileMgr->numLoadingTiles()/float(std::max(1, tileMgr->numTotalTiles()));
  if(currProgress >= 0 && currProgress < 1) {  // < 0 should not be possible ...
    if(progressWidget->isVisible())
      progressWidget->setProgress(currProgress);
    else if(!progressTimer) {
      progressTimer = gui->setTimer(1000, win.get(), progressTimer, [this](){
        progressTimer = NULL;
        progressWidget->setProgress(currProgress);
        progressWidget->setVisible(currProgress < 1);
        return 0;
      });
    }
  }
  else {
    if(progressTimer) {
      gui->removeTimer(progressTimer);
      progressTimer = NULL;
    }
    progressWidget->setVisible(false);
  }

  // prompt for 3D terrain if first time tilting
  if(cpos.tilt != 0 && !terrain3D && !config["terrain_3d"]["enabled"].IsDefined()) {
    config["terrain_3d"]["enabled"] = false;
    MapsApp::messageBox("3D Terrain",
        fstring("3D terrain can be controlled from the overflow menu.  Enable now?"), {"OK", "Cancel"},
        [=](std::string res){ if(res == "OK") { terrain3dCb->onClicked(); } });
  }

  sendMapEvent(MAP_CHANGE);
}

void MapsApp::onLowMemory()
{
  // could be more aggressive if suspended, e.g., completely unload scene
  map->onMemoryWarning();
}

void MapsApp::onLowPower(int state)
{
  lowPowerMode = state && cfg()["general"]["enable_low_power"].as<bool>(true);
}

void MapsApp::onSuspend()
{
  // send events here instead of from androidApp/iosApp.cpp just to eliminate cut and paste code
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;  // also send SDL_APP_WILLENTERBACKGROUND? SDL_APP_DIDENTERBACKGROUND?
  event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;  // closes menus, among other things
  gui->sdlEvent(&event);
  appSuspended = true;
  sendMapEvent(SUSPEND);
  saveConfig();
}

void MapsApp::onResume()
{
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;  // also send SDL_APP_WILLENTERFOREGROUND? SDL_APP_DIDENTERFOREGROUND?
  event.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
  gui->sdlEvent(&event);
  appSuspended = false;
  sendMapEvent(RESUME);
}

// Map::flyTo() zooms out and then back in, inappropriate for short flights
void MapsApp::gotoCameraPos(const CameraPosition& campos)
{
  if(followState == FOLLOW_ACTIVE)
    toggleFollow();

  Point scr;
  bool vis = map->lngLatToScreenPosition(campos.longitude, campos.latitude, &scr.x, &scr.y);
  if(!vis)
    prevCamPos = map->getCameraPosition();  // save prev position unless new position already on screen
  // if point is close enough, use simple ease instead of flyTo
  Rect viewport = getMapViewport();
  Point offset = scr - viewport.center();
  if(std::abs(offset.x) < 2*viewport.width() && std::abs(offset.y) < 2*viewport.height())
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
  map->markerSetProperties(locMarker, {{ {"hasfix", hasLocation ? 1 : 0}, {"selected", currLocPlaceInfo ? 1 : 0},
      {"orientation", orientation}, {"map_rotation", map->getRotation()*180/float(M_PI)}}});
}

void MapsApp::updateLocPlaceInfo()
{
  SvgText* coordnode = static_cast<SvgText*>(infoContent->containerNode()->selectFirst(".lnglat-text"));
  std::string locstr = lngLatToStr(currLocation.lngLat());
  if(currLocation.poserr > 0)
    locstr += fstring(" (\u00B1%.0f m)", currLocation.poserr);
  // m/s -> kph or mph
  if(!std::isnan(currLocation.spd))
    locstr += metricUnits ? fstring(" %.1f km/h", currLocation.spd*3.6)
        : fstring(" %.1f mph", currLocation.spd*2.23694);
  if(coordnode)
    coordnode->setText(locstr.c_str());

  SvgText* elevnode = static_cast<SvgText*>(infoContent->containerNode()->selectFirst(".currloc-elevation-text"));
  double elev = currLocation.alt;
  if(elevnode)
    elevnode->setText(elevToStr(elev).c_str());
}

void MapsApp::updateLocation(const Location& _loc)
{
  // we may get same location twice due to service for track recording
  double dt = _loc.time - currLocation.time;
  if(std::abs(dt) < 0.1 && _loc.lngLat() == currLocation.lngLat() && _loc.alt == currLocation.alt)
    return;

  double dr = lngLatDist(_loc.lngLat(), currLocation.lngLat());
  double dh = _loc.alt - currLocation.alt;
  float spd = currLocation.spd > 1.0f ? currLocation.spd : 1.0f;  // handle NaN
  if(_loc.poserr > dr && _loc.alterr > std::abs(dh) && _loc.poserr > currLocation.poserr + dt*spd) {
    LOGW("Rejecting location update: dt = %.3f s, dr = %.2f m, err = %.2f m", dt, dr, _loc.poserr);
    return;
  }

  if(lowPowerMode && dt < 1 && !mapsTracks->recordTrack && followState != FOLLOW_ACTIVE &&
      dh < 10 && dr < 5*MapProjection::metersPerPixelAtZoom(int(map->getZoom()))) {
    LOGD("Low Power mode - discarding location update: dt = %.3f s, dr = %.2f m, err = %.2f m", dt, dr, _loc.poserr);
    return;
  }

  locMarkerNeedsUpdate = true;
  Point prevpt = lngLatToScreenPoint(currLocation.lngLat());
  Point currpt = lngLatToScreenPoint(_loc.lngLat());
  Rect viewport = getMapViewport().pad(20);
  if(viewport.contains(prevpt) || viewport.contains(currpt)) {
    platform->requestRender();
  }

  currLocation = _loc;
  if(currLocation.time <= 0)
    currLocation.time = mSecSinceEpoch()/1000.0;
  if(appSuspended) {
    mapsTracks->onMapEvent(LOC_UPDATE);
    return;
  }
#if PLATFORM_IOS
  hasLocation = _loc.poserr > 0 && _loc.poserr < 30;  // no GPS status on iOS
#endif
  //updateLocMarker();

  if(followState == FOLLOW_ACTIVE) {
    map->setCameraPosition(map->getCameraPosition().setLngLat(_loc.lngLat()));  //, 0.1f);
  }
  if(currLocPlaceInfo) {
    updateLocPlaceInfo();
    pickResultCoord = _loc.lngLat();  // update coords for use when saving bookmark, waypoint, etc
  }
  if(initToCurrLoc) {
    recenterBtn->onClicked();
    initToCurrLoc = false;
  }

  sendMapEvent(LOC_UPDATE);
}

void MapsApp::updateGpsStatus(int satsVisible, int satsUsed)
{
  // only show if no fix yet; satsVisible < 0 if GPS disabled
  gpsStatusBtn->setVisible(satsUsed < 1 && satsVisible >= 0);
  if(satsUsed < 1 && satsVisible >= 0)
    gpsStatusBtn->setText(fstring("%d", satsVisible).c_str());  //"%d/%d", satsUsed
  bool hadloc = hasLocation;
  hasLocation = satsUsed > 0;  //|| (currLocation.poserr > 0 && currLocation.poserr < 10);  -- age of currLocation!?!
  //gpsSatsUsed = satsUsed;
  if(hasLocation != hadloc)
    updateLocMarker();
}

// values should be in degrees, not radians
void MapsApp::updateOrientation(double time, float azimuth, float pitch, float roll)
{
  float deg = azimuth - 360*std::floor(azimuth/360);
  float threshold = followState == FOLLOW_ACTIVE ? 0.1f :
      (lowPowerMode && time - orientationTime < 1 ? 5.0f : 1.0f);
  if(std::abs(deg - orientation) < threshold) { return; }
  orientation = deg;
  orientationTime = time;
  // we might have to add a low-pass for this
  if(followState == FOLLOW_ACTIVE) {
    auto campos = map->getCameraPosition();
    campos.rotation = -deg*float(M_PI)/180;
    map->setCameraPosition(campos); //, 0.1f);
  }
  //LOGW("orientation: %.1f deg", orientation);
  //updateLocMarker();
  locMarkerNeedsUpdate = true;
  if(getMapViewport().pad(20).contains(lngLatToScreenPoint(currLocation.lngLat()))) {
    platform->requestRender();
  }
}

void MapsApp::toggleFollow()
{
  bool follow = followState == NO_FOLLOW;
  followState = follow ? FOLLOW_PENDING : NO_FOLLOW;
  auto campos = map->getCameraPosition().setLngLat(currLocation.lngLat());
  campos.rotation = follow ? -orientation*float(M_PI)/180 : 0;
  map->setCameraPositionEased(campos, 1.0f);
  recenterBtn->setIcon(MapsApp::uiIcon(follow ? "nav-arrow" : "gps-location"));
  followGPSBtn->setChecked(follow);
}

const YAML::Node& MapsApp::sceneConfig()
{
  if(!map->getScene()->isReady() && !map->getScene()->isPendingCompletion()) {
    //assert(false);
    LOGW("MapsApp::sceneConfig(): scene still loading!");
  }
  return map->getScene()->config();
}

std::shared_ptr<TileSource> MapsApp::getElevationSource()
{
  auto& tileSources = map->getScene()->tileSources();
  for(const auto& srcname : config["sources"]["elevation"]) {
    for(auto& src : tileSources) {
      if(src->isRaster() && src->name() == srcname.Scalar())
        return src;
    }
  }
  return nullptr;
}

struct malloc_deleter { void operator()(void* x) { std::free(x); } };
struct ElevTex { std::unique_ptr<uint8_t, malloc_deleter> data; int width; int height; GLint fmt; };

static double readElevTex(const ElevTex& tex, int x, int y)
{
  // see getElevation() in hillshade.yaml and https://github.com/tilezen/joerd
  if(tex.fmt == GL_R32F)
    return ((float*)tex.data.get())[y*tex.width + x];
  GLubyte* p = tex.data.get() + y*tex.width*4 + x*4;
  //(red * 256 + green + blue / 256) - 32768
  return (p[0]*256 + p[1] + p[2]/256.0) - 32768;
}

static double elevationLerp(const ElevTex& tex, TileID tileId, LngLat pos)
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
  double x0 = ox*tex.width - 0.5, y0 = oy*tex.height - 0.5;  // -0.5 to adjust for pixel centers
  // we should extrapolate at edges instead of clamping - see shader in raster_contour.yaml
  int ix0 = std::max(0, int(std::floor(x0))), iy0 = std::max(0, int(std::floor(y0)));
  int ix1 = std::min(int(std::ceil(x0)), tex.width-1), iy1 = std::min(int(std::ceil(y0)), tex.height-1);
  double fx = x0 - ix0, fy = y0 - iy0;
  double t00 = readElevTex(tex, ix0, iy0);
  double t01 = readElevTex(tex, ix0, iy1);
  double t10 = readElevTex(tex, ix1, iy0);
  double t11 = readElevTex(tex, ix1, iy1);
  double t0 = t00 + fx*(t10 - t00);
  double t1 = t01 + fx*(t11 - t01);
  return t0 + fy*(t1 - t0);
}

double MapsApp::getElevation(LngLat pos, std::function<void(double)> callback)
{
  static ElevTex prevTex;
  static TileID prevTileId = {0, 0, 0, 0};

  if(prevTex.data) {
    TileID tileId = lngLatTile(pos, prevTileId.z);
    if(tileId == prevTileId) {
      double elev = elevationLerp(prevTex, tileId, pos);
      if(callback) { callback(elev); }
      return elev;
    }
  }

  auto* elevMgr = map->getScene()->elevationManager();
  if(elevMgr) {
    bool ok = false;
    double elev = elevMgr->getElevation(MapProjection::lngLatToProjectedMeters(pos), ok);
    if(ok) {
      if(callback) { callback(elev); }
      return elev;
    }
  }

  if(!callback) return 0;

  auto elevSrc = getElevationSource();
  if(!elevSrc) return 0;
  TileID tileId = lngLatTile(pos, elevSrc->maxZoom());
  // do not use RasterSource::createTask() because we can't use its cached Textures!
  auto task = std::make_shared<BinaryTileTask>(tileId, elevSrc.get());
  task->setScenePrana(map->getScene()->prana());
  elevSrc->loadTileData(task, {[=](std::shared_ptr<TileTask> _task) {
    runOnMainThread([=](){
      if(_task->hasData()) {
        auto& data = *static_cast<BinaryTileTask*>(_task.get())->rawTileData;
        prevTex.data.reset(Tangram::loadImage((const uint8_t*)data.data(),
            data.size(), &prevTex.width, &prevTex.height, &prevTex.fmt, 4));
        prevTileId = tileId;
        if(prevTex.data)
          callback(elevationLerp(prevTex, tileId, pos));
      }
    });
  }});
  return 0;
}

std::string MapsApp::elevToStr(double meters)
{
  return fstring(metricUnits ? "%.0f m" : "%.0f ft", metricUnits ? meters : meters*3.28084);
}

std::string MapsApp::distKmToStr(double dist, int prec, int sigdig)
{
  if(!MapsApp::metricUnits) {
    double miles = dist*0.621371;
    prec = std::min(std::max(sigdig - int(std::log10(miles)) - 1, 0), prec);
    if(miles < 0.1)
      return fstring("%.0f ft", 5280*miles);
    else
      return fstring("%.*f mi", (miles < 1 && prec < 1) ? 1 : prec, miles);
  }
  prec = std::min(std::max(sigdig - int(std::log10(dist)) - 1, 0), prec);
  if(dist < 0.1 || (dist < 1 && prec > 1))
    return fstring("%.0f m", dist*1000);
  return fstring("%.*f km", prec, dist);
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
    std::string filename = baseDir + ::fstring("dump_%s_%d_%d_%d.json", task->source()->name().c_str(), id.z, id.x, id.y);
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
      auto task = std::make_shared<BinaryTileTask>(tileId, src.get());
      task->setScenePrana(map->getScene()->prana());
      src->loadTileData(task, cb);
    }
  }
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

Button* MapsApp::addUndeleteItem(const std::string& title, const SvgNode* icon, std::function<void()> callback)
{
  auto menuitems = undeleteMenu->selectFirst(".child-container")->containerNode();
  if(menuitems->children().size() >= 10)
    gui->deleteWidget(static_cast<Widget*>(menuitems->children().front()->ext()));
  Button* item = undeleteMenu->addItem(title.c_str(), icon, {});
  item->onClicked = [=](){
    callback();
    gui->deleteWidget(item);
  };
  undeleteMenu->parent()->setVisible(true);
  return item;
}

void MapsApp::setWindowLayout(int fbWidth, int fbHeight)
{
  bool narrow = fbHeight > fbWidth && fbWidth/gui->paintScale < 700;
  if(currLayout && narrow == currLayout->node->hasClass("window-layout-narrow")) return;

  if(currLayout && !narrow && panelMaximized) {
    maximizePanel(false);  // un-maximize if going from narrow to wide
    panelMaximized = true;  // will restore when going back to narrow layout
  }
  bool wasvis = panelContainer && panelContainer->isVisible();
  if(currLayout) currLayout->setVisible(false);
  currLayout = win->selectFirst(narrow ? ".window-layout-narrow" : ".window-layout-wide");
  currLayout->setVisible(true);
#if PLATFORM_IOS
  if(!narrow)
    currLayout->selectFirst(".panel-layout")->setMargins(12, 0, 0, 50);
#endif

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

  // adjust scrollwidgets
  auto scrollWidgets = panelContent->select(".scrollwidget-var-height");
  for(Widget* sw : scrollWidgets)
    sw->node->setAttribute("box-anchor", narrow ? "fill" : "hfill");

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

  // let's try hiding panel title icons if we also show the main toolbar w/ the same icon
  auto panelicons = panelContent->select(".panel-icon");
  for(Widget* btn : panelicons)
    btn->setVisible(narrow);

  showPanelContainer(wasvis);
  if(narrow && panelMaximized)
    maximizePanel(true);
}

void MapsApp::createGUI(SDL_Window* sdlWin)
{
  static const char* mainWindowSVG = R"#(
    <svg class="window" layout="box">
      <g class="window-layout-narrow" display="none" box-anchor="fill" layout="flex" flex-direction="column">
        <g class="maps-container" box-anchor="fill" layout="box"></g>
        <rect class="panel-splitter background splitter" display="none" box-anchor="hfill" width="10" height="0"/>
        <g class="panel-container panel-container-narrow" display="none" box-anchor="hfill" layout="box">
          <rect class="panel-bg background" box-anchor="fill" x="0" y="0" width="20" height="20" margin="20 0 0 0" />
          <rect class="results-split-sizer" fill="none" box-anchor="hfill" width="320" height="200"/>
        </g>
        <g class="main-tb-container" box-anchor="hfill" margin="0 20 16 20" layout="box"></g>
        <rect class="bottom-inset" box-anchor="hfill" width="20" height="0"/>
      </g>

      <g class="window-layout-wide" display="none" box-anchor="fill" layout="box">
        <g class="maps-container" box-anchor="fill" layout="box"></g>
        <g class="panel-layout" box-anchor="top left" margin="20 0 0 20" layout="flex" flex-direction="column">
          <g class="main-tb-container" box-anchor="hfill" layout="box">
            <rect class="background" fill="none" x="0" y="0" width="360" height="1"/>
          </g>
          <rect class="panel-separator hrule title background" display="none" box-anchor="hfill" width="20" height="2"/>
          <g class="panel-container panel-container-wide" display="none" box-anchor="hfill" layout="box">
            <rect class="background panel-bg" box-anchor="fill" x="0" y="0" width="20" height="20"/>
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
      <g class="toolbutton-content" margin="0 0" box-anchor="fill" layout="box">
        <use class="icon" width="28" height="28" xlink:href=":/ui-icons.svg#compass" />
      </g>
    </g>
  )#";

  Tooltips::inst = &tooltipsInst;
  TextEdit::defaultMaxLength = 65536;

  SvgDocument* winnode = createWindowNode(mainWindowSVG);
  win.reset(new Window(winnode));
  win->sdlWindow = sdlWin;
  win->isFocusable = true;  // top level window should always be focusable

  win->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
      getSafeAreaInsets(&topInset, &bottomInset);
      if(bottomInset > 0)
        static_cast<SvgRect*>(bottomPadding->node)->setRect(Rect::wh(20, bottomInset));
      return true;
    }
    return false;
  });

  panelContent = createBoxLayout();
  panelPager = new Pager(winnode->selectFirst(".panel-container-narrow"));
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
  panelPager->onPageChanged = [this](Widget* page){ showPanel(page); };
  // std::move() clears source fn in GCC but not clang - https://bugs.llvm.org/show_bug.cgi?id=33125
  pagerEventFilter.swap(panelPager->eventFilter);  // we set event filter on panel toolbar instead

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

  bottomPadding = new Widget(winnode->selectFirst(".bottom-inset"));

  // adjust map center to account for sidebar
  //map->setPadding({200, 0, 0, 0});

  customColorDialog.reset(new ManageColorsDialog(markerColors));
  customColorDialog->onColorListChanged = [this](){
    populateColorPickerMenu();
    auto& node = config["colors"] = YAML::Array();  //YAML::NodeType::Sequence);
    for(Color& color : markerColors)
      node.push_back( colorToStr(color) );
  };
  populateColorPickerMenu();

  infoContent = new Widget(loadSVGFragment(R"#(<g layout="box" box-anchor="hfill"></g>)#"));
  auto infoHeader = createPanelHeader(NULL, "");  //MapsApp::uiIcon("pin"), "");
  auto infoTitle = infoHeader->selectFirst(".panel-title");
  infoTitle->node->setAttribute("box-anchor", "left");  // disable elision
  infoTitle->parent()->addWidget(createStretch());
  infoPanel = createMapPanel(infoHeader, infoContent);
  infoPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == MapsApp::PANEL_CLOSED)
      clearPickResult();
    return false;
  });

  // toolbar w/ buttons for search, bookmarks, tracks, sources
  mainToolbar = createMenubar();  //createToolbar();
  mainToolbar->node->addClass("main-toolbar");
  mainToolbar->selectFirst(".child-container")->node->setAttribute("justify-content", "space-between");
  mainToolbar->selectFirst(".child-container")->setMargins(0, 10);
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
  Button* metricCb = createCheckBoxMenuItem("Metric units");
  metricCb->onClicked = [=](){
    metricUnits = !metricUnits;
    config["metric_units"] = metricUnits;
    metricCb->setChecked(metricUnits);
    mapsSources->rebuildSource(mapsSources->currSource);  //loadSceneFile();
    while(!panelHistory.empty()) popPanel();  // to force update of panels
  };
  metricCb->setChecked(metricUnits);
  overflowMenu->addItem(metricCb);

  terrain3dCb = createCheckBoxMenuItem("3D terrain");
  terrain3dCb->onClicked = [=](){
    terrain3D = !terrain3D;
    config["terrain_3d"]["enabled"] = terrain3D;
    terrain3dCb->setChecked(terrain3D);
    mapsSources->rebuildSource(mapsSources->currSource);  //loadSceneFile();
  };
  terrain3dCb->setChecked(terrain3D);
  overflowMenu->addItem(terrain3dCb);

  Button* themeCb = createCheckBoxMenuItem("Light theme");
  themeCb->onClicked = [=](){
    bool light = !themeCb->checked();
    themeCb->setChecked(light);
    config["ui"]["theme"] = light ? "light" : "dark";
    SvgCssStylesheet* ss = createStylesheet(light);
    gui->setWindowStylesheet(std::unique_ptr<SvgCssStylesheet>(ss));
  };
  overflowMenu->addItem(themeCb);

  undeleteMenu = createMenu(Menu::HORZ);
  overflowMenu->addSubmenu("Undelete", undeleteMenu);
  undeleteMenu->parent()->setVisible(false);  // hidden when empty

#ifdef NDEBUG
  if(config["ui"]["show_debug"].as<bool>(false))
#endif
  {
    Menu* debugMenu = createMenu(Menu::HORZ);
    const char* debugFlags[9] = {"Freeze tiles", "Proxy colors", "Tile bounds", "Label bounds",
        "Tangram info", "Draw all labels", "Tangram stats", "Selection buffer", "Depth buffer"};
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
      double t = mSecSinceEpoch()/1000.0;
      double lat = currLocation.lat + 0.00005*(0.5 + std::rand()/real(RAND_MAX));
      double lng = currLocation.lng + 0.00005*(0.5 + std::rand()/real(RAND_MAX));
      double alt = currLocation.alt + 10*std::rand()/real(RAND_MAX);
      updateLocation(Location{t, lat, lng, 10, alt, 10, NAN, 0, NAN, 0});
      updateOrientation(t, orientation + 5*std::rand()/real(RAND_MAX), 0, 0);
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
      bool offline = !offlineCb->isChecked();
      offlineCb->setChecked(offline);
      platform->isOffline = offline;
      config["force_offline"] = offline;
    };
    appDebugMenu->addItem(offlineCb);
    if(config["force_offline"].as<bool>(false))
      offlineCb->onClicked();

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
      Point center = getMapViewport().center();
      dumpTileContents(center.x, center.y);
    });
    appDebugMenu->addItem("Dump YAML", [this](){
      std::string filename = baseDir + "dump_scene.yaml";
      std::ofstream fout(filename);
      fout << yamlToStr(map->getScene()->config(), 10);
      LOGW("Scene YAML dumped to %s", filename.c_str());
    });
    appDebugMenu->addItem("Print JS stats", [this](){ dumpJSStats(map->getScene()); });
    appDebugMenu->addItem("Set location", [this](){
      Location loc(currLocation);
      if(!std::isnan(pickResultCoord.latitude)) {
        loc.lng = pickResultCoord.longitude;
        loc.lat = pickResultCoord.latitude;
      }
      else
        map->getPosition(loc.lng, loc.lat);
      updateLocation(loc);
    });
    overflowMenu->addSubmenu("App debug", appDebugMenu);
  }

  mainToolbar->addButton(overflowBtn);

  // map widget and floating btns
  mapsWidget = new MapsWidget(this);
  mapsWidget->node->setAttribute("box-anchor", "fill");
  mapsWidget->isFocusable = true;
  mapsContent->addWidget(mapsWidget);

  // put handler on container so crosshair, etc. don't swallow pan
  mapsContent->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERDOWN)
      gui->setPressed(mapsContent);
    return touchHandler->sdlEvent(gui, event);
  });

  // recenter, reorient btns
  // we could switch to different orientation modes (travel direction, compass direction) w/ multiple taps
  reorientBtn = new Button(loadSVGFragment(reorientSVG));  //createToolbutton(MapsApp::uiIcon("compass"), "Reorient");
  reorientBtn->setMargins(0, 0, 6, 0);
  reorientBtn->onClicked = [this](){
    LngLat center = getMapCenter();
    CameraPosition campos = {center.longitude, center.latitude, map->getZoom(), 0, 0};
    map->setCameraPositionEased(campos, 1.0);
    if(followState != NO_FOLLOW) {
      followState = NO_FOLLOW;
      recenterBtn->setIcon(MapsApp::uiIcon("gps-location"));
    }
  };
  reorientBtn->setVisible(false);
  // prevent complete UI layout when compass icon is rotated
  reorientBtn->selectFirst(".toolbutton-content")->layoutIsolate = true;

  // Recenter (i.e. jump to GPS location) button

  Menu* recenterMenu = createMenu(Menu::VERT);
  Button* enableGPSBtn = createCheckBoxMenuItem("Enable GPS");
  enableGPSBtn->onClicked = [=](){
    sensorsEnabled = !sensorsEnabled;
    setSensorsEnabled(sensorsEnabled);
    enableGPSBtn->setChecked(sensorsEnabled);
    recenterBtn->setIcon(MapsApp::uiIcon(sensorsEnabled ? "gps-location" : "gps-location-off"));
    hasLocation = false;  // if enabling, still need to wait for GPS status update
    if(!sensorsEnabled) {
      gpsStatusBtn->setVisible(false);
      updateLocMarker();
    }
  };
  enableGPSBtn->setChecked(true);
  recenterMenu->addItem(enableGPSBtn);

  followGPSBtn = createCheckBoxMenuItem("Follow");
  followGPSBtn->onClicked = [this](){ toggleFollow(); };
  recenterMenu->addItem(followGPSBtn);

  recenterMenu->addItem("Previous View", [this](){ gotoCameraPos(CameraPosition(prevCamPos)); });

  //recenterBtn = createToolbutton(MapsApp::uiIcon("gps-location"), "Recenter");
  recenterBtn = new Button(widgetNode("#roundbutton"));
  recenterBtn->setIcon(MapsApp::uiIcon("gps-location"));
  recenterBtn->onClicked = [=](){
    if(!sensorsEnabled) {
      setSensorsEnabled(true);
      sensorsEnabled = true;
      enableGPSBtn->setChecked(true);
      recenterBtn->setIcon(MapsApp::uiIcon("gps-location"));
      return;
    }
    if(followState != NO_FOLLOW)
      return;

    auto campos = map->getCameraPosition().setLngLat(currLocation.lngLat());
    //campos.zoom = std::min(campos.zoom, 16.0f);
    Point loc;
    bool locvisible = map->lngLatToScreenPosition(currLocation.lng, currLocation.lat, &loc.x, &loc.y);
    if(loc.dist(getMapViewport().center())/gui->paintScale < 20) {  // && cammatch
      campos.zoom = std::min(20.0f, campos.zoom + 1);
      map->setCameraPositionEased(campos, 0.35f);
    }
    else if(!locvisible && !std::isnan(pickResultCoord.latitude) &&
        map->lngLatToScreenPosition(pickResultCoord.longitude, pickResultCoord.latitude)) {
      auto viewboth = map->getEnclosingCameraPosition(pickResultCoord, currLocation.lngLat());
      // -1 since currLocation is placed at center not edge; -0.25 or -0.5 for padding
      campos.zoom = viewboth.zoom - (terrain3D ? 1.5f : 1.25f);
      gotoCameraPos(campos);  //, 1.0);
    }
    else {
      if(campos.zoom < 12) { campos.zoom = 15; }
      if(flyingToCurrLoc)
        map->setCameraPosition(campos);  // stop animation and go to final position immediately
      else
        gotoCameraPos(campos);  //, 1.0);
      flyingToCurrLoc = !flyingToCurrLoc;
    }
  };

  recenterBtn->addWidget(recenterMenu);
  SvgGui::setupRightClick(recenterBtn, [=](SvgGui* gui, Widget* w, Point p){
    gui->showMenu(recenterMenu);  //gui->showContextMenu(recenterMenu, p, w);
    gui->setPressed(recenterMenu);
    recenterBtn->node->setXmlClass(
        addWord(removeWord(recenterBtn->node->xmlClass(), "hovered"), "pressed").c_str());
  });

  // GPS status button (show satellite status when searching for position)

  gpsStatusBtn = new Widget(loadSVGFragment(gpsStatusSVG));
  gpsStatusBtn->setMargins(0, 0, 6, 0);
  gpsStatusBtn->setVisible(false);

  progressWidget = new ProgressCircleWidget;
  progressWidget->setMargins(0, 0, 6, 0);
  progressWidget->setVisible(false);

  bool revbtns = config["ui"]["reverse_map_btns"].as<bool>(false);
  Widget* floatToolbar = createColumn({progressWidget, gpsStatusBtn, reorientBtn, recenterBtn});
  floatToolbar->node->setAttribute("box-anchor", revbtns ? "bottom left" : "bottom right");
  floatToolbar->setMargins(10, 10);
  floatToolbar->layoutIsolate = true;
  mapsContent->addWidget(floatToolbar);

  scaleBar = new ScaleBarWidget(map.get());
  scaleBar->node->setAttribute("box-anchor", revbtns ? "bottom right" : "bottom left");
  scaleBar->setMargins(6, 10);
  mapsContent->addWidget(scaleBar);

  crossHair = new CrosshairWidget();
  crossHair->setVisible(false);
  crossHair->layoutIsolate = true;  // this doesn't really help since distance text also changes
  mapsContent->addWidget(crossHair);

  legendContainer = createColumn();
  legendContainer->node->setAttribute("box-anchor", "hfill bottom");
  legendContainer->node->addClass("legend");
  legendContainer->setMargins(0, 0, 14, 0);  // shift above scale bar
  mapsContent->addWidget(legendContainer);

  // misc setup
  placeInfoProviderIdx = pluginManager->placeFns.size();
  // set window stylesheet
  themeCb->setChecked(config["ui"]["theme"].as<std::string>("") != "light");
  themeCb->onClicked();

  // need to update map sources from old version ... there will probably be changes to default sources
  //  in every release for the forseeable future
  if(prevVersion < versionCode) {  //if(prevVersion == 1) {
    FSPath path = FSPath(configFile).parent().child("mapsources.default.yaml");
    mapsSources->syncImportFile(path.path);
  }

  gui->showWindow(win.get(), NULL);

#if PLATFORM_ANDROID
  if(prevVersion == 0) {
    MapsApp::messageBox("Choose folder", "To use an external data folder, enable All Files Access for Ascend"
        " in Android settings, then choose OK to select a folder.", {"OK", "Cancel"},
        [=](std::string res){
          if(res != "OK") { return; }
          MapsApp::pickFolderDialog([this](const char* path){
            PLATFORM_LOG("Selected data folder: %s\n", path);
            if(!FSPath(path, ".nomedia").exists("wb")) {
              messageBox("Error", "Unable to write to selected folder - All Files Access not enabled?", {"OK"});
              config["prev_version"] = 0;  // try again when reopened
              return;
            }
            config["base_directory"] = path;
            saveConfig();
            messageBox("Reopen", "Please reopen the application to use new data folder.", {"OK"});
                //[](std::string){ runApplication = false; });  -- just freezes app w/o exiting
          });
        });
  }
#endif

  // on desktop, command line options could override startup behavior
#if PLATFORM_MOBILE
  mapsOffline->resumeDownloads();
  mapsSources->rebuildSource(config["sources"]["last_source"].Scalar());
#endif
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
    bottomPadding->setVisible(show);
  }
  if(!panelHistory.empty() && show)
    panelHistory.back()->setVisible(true);  // to send VISIBLE event to panel
  panelMinimized = !panelHistory.empty() && !show;
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
  bool wasminimized = panelMinimized && !panelHistory.empty();  // preserve minimized state
  panel->setVisible(true);
  panelHistory.push_back(panel);
  showPanelContainer(true);
  panelMinimized = wasminimized;
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

  if(!panelHistory.empty() && panelHistory.back() == panelToSkip) {
    // hack to handle the few cases where we want to go directly to a subpanel (e.g. directions)
    panelToSkip = NULL;
    return popPanel();
  }
  if(panelHistory.empty() || panelMinimized)
    showPanelContainer(false);
  else
    panelHistory.back()->setVisible(true);

  return true;
}

void MapsApp::maximizePanel(bool maximize)
{
  panelMaximized = maximize;
  if(!currLayout->node->hasClass("window-layout-narrow") || panelHistory.empty()) return;
#if PLATFORM_MOBILE
  Widget* panelhdr = panelHistory.back()->selectFirst(".panel-header");
  if(panelhdr)
    panelhdr->selectFirst(".child-container")->setMargins(maximize ? topInset : 0, 0, 0, 0);
#endif
  currLayout->selectFirst(".maps-container")->setVisible(!maximize);
  //currLayout->selectFirst(".top-inset")->setVisible(PLATFORM_MOBILE && maximize);
  panelContainer->node->setAttribute("box-anchor", maximize ? "fill" : "hfill");
  panelSplitter->setEnabled(!maximize);
  bool isLight = config["ui"]["theme"].as<std::string>("") == "light";
  // top-inset uses panel header color, which is inverted from theme
  notifyStatusBarBG(maximize ? !isLight : !sceneConfig()["application"]["dark_base_map"].as<bool>(false));

  //Widget* minbtn = panelHistory.back()->selectFirst(".minimize-btn");
  //if(minbtn)
  //  minbtn->setVisible(!maximize);
}

// make this a static method or standalone fn?
Toolbar* MapsApp::createPanelHeader(const SvgNode* icon, const char* title)
{
  Toolbar* toolbar = new Toolbar(widgetNode("#panel-header"));
  auto backBtn = createToolbutton(MapsApp::uiIcon("back"));
  backBtn->onClicked = [this](){ popPanel(); };
  toolbar->addWidget(backBtn);
  Widget* titleWidget = new Widget(widgetNode("#panel-header-title"));
  // need widget to show/hide icon in setWindowLayout()
  Widget* iconWidget = new Widget(titleWidget->node->selectFirst(".panel-icon"));
  if(icon)
    static_cast<SvgUse*>(iconWidget->node)->setTarget(icon);
  TextLabel* titleLabel = new TextLabel(titleWidget->node->selectFirst(".panel-title"));
  titleLabel->setText(title);
  //static_cast<SvgText*>(titleWidget->containerNode()->selectFirst("text"))->setText(title);
  toolbar->addWidget(titleWidget);

  toolbar->addHandler([=](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      gui->setPressed(toolbar);  // accept press event if a child hasn't
    }
    else if(event->type == SDL_FINGERMOTION && event->tfinger.fingerId == SDL_BUTTON_LMASK
        //&& event->tfinger.touchId != SDL_TOUCH_MOUSEID  -- at least need to allow splitter!!!
        && gui->menuStack.empty()  // don't interfere with menu
        && gui->fingerClicks == 0  // require sufficient motion before activation
        && (!gui->pressedWidget || gui->pressedWidget->isDescendantOf(toolbar))) {
      if(gui->pressedWidget)
        gui->pressedWidget->sdlUserEvent(gui, SvgGui::OUTSIDE_PRESSED, 0, event, NULL);  //this);
      auto& p0 = gui->pressEvent.tfinger;
      auto& p1 = event->tfinger;
      if(std::abs(p1.x-p0.x) > std::abs(p1.y-p0.y)) {
        pagerEventFilter(gui, toolbar, &gui->pressEvent);  // send to Pager
        gui->setPressed(panelPager);
        return pagerEventFilter(gui, toolbar, event);
      }
      else {
        panelSplitter->sdlEvent(gui, &gui->pressEvent);  // send to splitter
      }
    }
    else
      return false;
    return true;
  });

  toolbar->eventFilter = [=](SvgGui* gui, Widget* widget, SDL_Event* event){
    if(gui->pressedWidget == panelPager)
      return pagerEventFilter(gui, widget, event);
    if(gui->pressedWidget == panelSplitter || gui->pressedWidget == toolbar)
      return false;
    if(event->type == SDL_FINGERMOTION && event->tfinger.fingerId == SDL_BUTTON_LMASK)
      return toolbar->sdlEvent(gui, event);  // send to splitter
    return false;
  };

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
    if(btn->isChecked()) {
      if(panelHistory.back()->node->hasClass("can-minimize"))
        showPanelContainer(!panelContainer->isVisible());
      else
        while(!panelHistory.empty()) popPanel();
    }
    else
      showPanel(panel);
  };

  return btn;
}

Widget* MapsApp::createMapPanel(Toolbar* header, Widget* content, Widget* fixedContent, bool canMinimize)
{
  // on mobile, easier to just swipe down to minimize instead of tapping button
  //if(PLATFORM_DESKTOP && canMinimize) {
  //  auto minimizeBtn = createToolbutton(MapsApp::uiIcon("chevron-down"));
  //  minimizeBtn->node->addClass("minimize-btn");
  //  minimizeBtn->onClicked = [this](){ showPanelContainer(false); };
  //  header->addWidget(minimizeBtn);
  //}

  Widget* panel = createColumn();
  if(canMinimize) panel->node->addClass("can-minimize");
  panel->node->setAttribute("box-anchor", "fill");
  panel->addWidget(header);
  panel->addWidget(createHRule(2, "", "panel-hrule"));
  if(fixedContent)
    panel->addWidget(fixedContent);
  if(content) {
    real fixedh = fixedContent ? fixedContent->node->bounds().height() : 0;
    content->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only
    auto* scrollWidget = createScrollWidget(content, 120, -160 - fixedh);
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
  // stylesheet must be set to measure text
  dialog->documentNode()->setStylesheet(gui->windowStylesheet);
  dialog->documentNode()->restyle();
  // wrap message text as needed
  std::string bmsg = SvgPainter::breakText(msgNode, std::min(800.0, 0.8*gui->windows.front()->winBounds().width()));
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
  return ::uiIcon(id);
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

#include "util/yamlUtil.h"

bool MapsApp::loadConfig(const char* assetPath)
{
  FSPath configPath(baseDir, "config.yaml");
  FSPath configDfltPath(baseDir, "config.default.yaml");
  bool firstrun = !configDfltPath.exists();
  if(firstrun)
    extractAssets(assetPath);

  config = YAML::LoadFile(configPath.exists() ? configPath.path : configDfltPath.path);
  if(!config) {
    LOGE("Unable to load config file %s", configPath.c_str());
    taskQueue.push_back([](){ messageBox("Error",
        "Error loading config!  Restore config.yaml or reinstall the application.", {"OK"}); });
    //*(volatile int*)0 = 0;  //exit(1) -- Android repeatedly restarts app
    return false;  // do not set configFile so we don't write to it!
  }

  std::string newdir = config["base_directory"].as<std::string>("");
  // make sure we can still write the external directory
  if(!newdir.empty() && FSPath(newdir, ".nomedia").exists("wb")) {
    baseDir = FSPath(newdir, "").path;
    return loadConfig(baseDir.c_str());
  }

  configFile = configPath.c_str();
  prevVersion = config["prev_version"].as<int>(0);
  // set prev_version < 0 to update assets every run
  if(prevVersion < versionCode && prevVersion >= 0)
    config["prev_version"] = versionCode;

  // merge in new config.default.yaml
  if(prevVersion < versionCode && !firstrun) {
    extractAssets(assetPath);
    auto newconfig = YAML::LoadFile(configDfltPath.path);
    if(newconfig) {
      Tangram::YamlUtil::mergeMapFields(newconfig, std::move(config));
      config = std::move(newconfig);
    }
  }

  return prevVersion < versionCode;
}

// note that we need to saveConfig whenever app is paused on mobile, so easiest for MapsComponents to just
//  update config as soon as change is made (vs. us having to broadcast a signal on pause)
void MapsApp::saveConfig()
{
  config["storage"]["offline"] = storageOffline.load();
  config["storage"]["total"] = storageTotal.load();

  CameraPosition pos = map->getCameraPosition(true);  // get 2D pos since pos restored before scene loaded
  // should never happen, but if camera position is corrupted, don't persist it!
  if(!std::isnan(pos.longitude) && !std::isnan(pos.latitude)
       && pos.zoom > 0 && !std::isnan(pos.rotation) && !std::isnan(pos.tilt)) {
    auto& view = config["view"];
    view["lng"] = pos.longitude;
    view["lat"] = pos.latitude;
    view["zoom"] = pos.zoom;
    view["rotation"] = pos.rotation;
    view["tilt"] = pos.tilt;
  }

  config["ui"]["split_size"] = int(panelSplitter->currSize);

  std::string s = yamlToStr(config, 10);
  FileStream fs(configFile.c_str(), "wb");
  fs.write(s.c_str(), s.size());
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
  terrain3D = config["terrain_3d"]["enabled"].as<bool>(false);
  // Google Maps and Apple Maps use opposite scaling for this gesture, so definitely needs to be configurable
  touchHandler->dblTapDragScale = config["gestures"]["dbl_tap_drag_scale"].as<float>(1.0f);
  shuffleSeed = config["random_shuffle_seed"].as<bool>(true) ? std::rand() : 0;

  initResources(baseDir.c_str());

  gui = new SvgGui();
  // preset colors for tracks and bookmarks
  for(const auto& colorstr : config["colors"])
    markerColors.push_back(parseColor(colorstr.Scalar()));

  // DB setup
#if PLATFORM_ANDROID
  sqlite3_fdvfs_init("fdvfs", 0, NULL);
#endif
  //sqlite3_config(SQLITE_CONFIG_URI, 1);  -- enable at compile time instead (here is too late on Android)
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

  // create required folders
  createPath(FSPath(baseDir, "cache")); //, 0777);
  createPath(FSPath(baseDir, "tracks")); //, 0777);
  createPath(FSPath(baseDir, ".trash"));  //, 0777);
  removeDir(FSPath(baseDir, ".trash"), false);  // empty trash

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

  // GLES 3 needed on iOS for highp float textures
  //Tangram::ShaderSource::glesVersion = 300;  ... now detected in gl/hardware.cpp
  map = std::make_unique<Tangram::Map>(std::unique_ptr<Platform>(_platform));
  // Scene::onReady() remains false until after first call to Map::update()!
  //map->setSceneReadyListener([this](Tangram::SceneID id, const Tangram::SceneError*) {});
  //map->setCameraAnimationListener([this](bool finished){ sendMapEvent(CAMERA_EASE_DONE); });
  map->setPickRadius(2.0f);

  tracksDataSource = std::make_shared<ClientDataSource>(
      *platform, "tracks", "", false, TileSource::ZoomOptions(-1, -1, 14, 0));

  // Setup UI panels
  mapsSources = std::make_unique<MapsSources>(this);
  mapsOffline = std::make_unique<MapsOffline>(this);
  pluginManager = std::make_unique<PluginManager>(this);
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
  if(!config["view"]["lng"]) initToCurrLoc = true;
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

  if(nvglFB)
    nvgluDeleteFramebuffer(nvglFB);
  if(nvglBlit)
    nvgswuDeleteBlitter(nvglBlit);
}

bool MapsApp::drawFrame(int fbWidth, int fbHeight)
{
  using Tangram::FrameInfo;

  std::function<void()> queuedFn;
  while(taskQueue.pop_front(queuedFn))
    queuedFn();

  if(!runApplication || appSuspended) return false;

  if(glNeedsInit) {
    glNeedsInit = false;
    map->setupGL();
    // Painter created here since GL context required to build shaders
    // ALIGN_SCISSOR needed only due to rotated direction-icon inside scroll area
    if(config["ui"]["gpu_render"].as<bool>(true)) {
      nvglFB = nvgluCreateFramebuffer(NULL, 0, 0, NVGLU_NO_NVG_IMAGE | nvglFBFlags);
      nvglBlit = nvgswuCreateBlitter();
      painter.reset(new Painter(Painter::PAINT_GL | Painter::CACHE_IMAGES | Painter::ALIGN_SCISSOR));
      painter->setBackgroundColor(Color::TRANSPARENT_COLOR);
      //scaleBarPainter.reset(new Painter(Painter::PAINT_GL));
      //scaleBar->useDirectDraw = true;
      //crossHair->useDirectDraw = true;
      //gui->fullRedraw = true;
    }
    else
      painter.reset(new Painter(Painter::PAINT_SW | Painter::SW_BLIT_GL | Painter::CACHE_IMAGES | Painter::ALIGN_SCISSOR));
    painter->setAtlasTextThreshold(24 * gui->paintScale);  // 24px font is default for dialog titles
  }

  setWindowLayout(fbWidth, fbHeight);

  if(flyToPickResult) {
    // ensure marker is visible and hasn't been covered by opening panel
    Point scr;
    auto campos = map->getCameraPosition().setLngLat(pickResultCoord);
    bool onscr = map->lngLatToScreenPosition(campos.longitude, campos.latitude, &scr.x, &scr.y);
    bool underpanel = panelContainer->node->bounds().contains(scr/gui->paintScale);
    if(!onscr || underpanel) {
      if(!underpanel)
        campos.zoom = std::min(campos.zoom, 16.0f);
      gotoCameraPos(campos);
    }
    flyToPickResult = false;
  }

  // map rendering moved out of layoutAndDraw since object selection (which can trigger UI changes) occurs during render!
  bool mapdirty = platform->notifyRender();
  if(mapdirty) {
    auto now = std::chrono::high_resolution_clock::now();
    double currTime = std::chrono::duration<double>(now.time_since_epoch()).count();
    mapUpdate(currTime);
    //mapsWidget->redraw();  -- so we can draw unchanged UI over map
    scaleBar->redraw();  //if(!scaleBarPainter) {  }
  }

  FrameInfo::begin("UI update");
  painter->deviceRect = Rect::wh(fbWidth, fbHeight);
  Rect dirty = gui->layoutAndDraw(painter.get());
  FrameInfo::end("UI update");

  if(!mapdirty && !dirty.isValid())
    return false;

  //Rect scissor = mapsWidget->viewport*gui->paintScale;
  //nvgluSetScissor(0, 0, int(scissor.width() + 0.5), std::min(int(scissor.height() + 10), fbHeight));
  map->render();
  //nvgluSetScissor(0, 0, 0, 0);  // disable scissor
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

  FrameInfo::begin("UI render");
  if(painter->usesGPU()) {
    if(nvglFB) {
      if(dirty.isValid()) {
        int prevFBO = nvgluBindFramebuffer(nvglFB);
        nvgluSetFramebufferSize(nvglFB, fbWidth, fbHeight, nvglFBFlags);
        if(dirty != painter->deviceRect)
          nvgluSetScissor(int(dirty.left), fbHeight - int(dirty.bottom), int(dirty.width()), int(dirty.height()));
        nvgluClear(nvgRGBA(0, 0, 0, 0));
        painter->endFrame();
        nvgluSetScissor(0, 0, 0, 0);  // disable scissor for blit
        nvgluBindFBO(prevFBO);
      }
      nvgswuSetBlend(1);
      nvgswuBlitTex(nvglBlit, nvgluGetTexture(nvglFB), 0);
    }
    else
      painter->endFrame();
  }
  else {  // nanovg_sw renderer
    if(dirty.isValid()) {
      painter->setBackgroundColor(Color::INVALID_COLOR);
      Rect clear = 2*dirty.width() > fbWidth ? Rect::ltrb(0, dirty.top, fbWidth, dirty.bottom) : dirty;
      painter->targetImage->fillRect(clear, 0);
      painter->endFrame();
    }
    painter->blitImageToScreen(dirty, true);
  }
  FrameInfo::end("UI render");
  return true;
}
