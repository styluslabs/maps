#include "tracks.h"
#include "mapsapp.h"
#include "util.h"
#include "mapwidgets.h"
#include "plugins.h"

#include <ctime>
#include <iomanip>
#include "pugixml.hpp"
#include "yaml-cpp/yaml.h"
#include "sqlite3/sqlite3.h"
#include "util/yamlPath.h"
#include "nfd.hpp"  // file dialogs

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "usvg/svgpainter.h"
#include "usvg/svgparser.h"  // for parseColor
#include "usvg/svgwriter.h"  // for serializeColor

class TrackPlot : public Widget
{
public:
  TrackPlot();
  void draw(SvgPainter* svgp) const override;
  Rect bounds(SvgPainter* svgp) const override;
  void setTrack(const std::vector<Waypoint>& locs);
  real plotPosToTrackPos(real s);
  real trackPosToPlotPos(real s);

  std::function<void(real)> onHovered;
  std::function<void()> onPanZoom;

  Path2D altDistPlot, altTimePlot, spdDistPlot, spdTimePlot;
  Rect mBounds;
  double minAlt, maxAlt;
  float minSpd, maxSpd;
  double minTime, maxTime;
  double maxDist;
  bool plotVsDist = false;
  bool plotAlt = true;
  bool plotSpd = false;

  real zoomScale = 1;
  real zoomOffset = 0;
  real maxZoom = 1;

  //static Color bgColor;

private:
  real prevCOM = 0;
  real prevPinchDist = 0;
  mutable real plotWidth = 100;

  void updateZoomOffset(real dx);
};

TrackPlot::TrackPlot() : Widget(new SvgCustomNode), mBounds(Rect::wh(200, 200))
{
  // TODO: this is now used in three places - deduplicate!
  onApplyLayout = [this](const Rect& src, const Rect& dest){
    mBounds = dest.toSize();
    if(src != dest) {
      m_layoutTransform.translate(dest.left - src.left, dest.top - src.top);
      node->invalidate(true);
    }
    return true;
  };

  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::MULTITOUCH) {
      auto points = static_cast<std::vector<SDL_Finger>*>(event->user.data2);
      if(points->size() != 2) return false;
      SDL_Finger& pt1 = points->front();
      SDL_Finger& pt2 = points->back();
      real pinchcenter = (pt1.x - pt2.x)/2;
      real pinchdist = std::abs(pt1.x - pt2.x);
      SDL_Event* fevent = static_cast<SDL_Event*>(event->user.data1);
      if(fevent->tfinger.type == SDL_FINGERMOTION) {
        //zoomOffset += pinchcenter - prevCOM;
        zoomScale = std::max(1.0, zoomScale*pinchdist/prevPinchDist);
        updateZoomOffset(pinchcenter - prevCOM);
        redraw();
      }
      prevCOM = pinchcenter;
      prevPinchDist = pinchdist;
    }
    else if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      prevCOM = event->tfinger.x;
      gui->setPressed(this);
    }
    else if(event->type == SDL_FINGERMOTION && gui->pressedWidget == this) {
      updateZoomOffset(event->tfinger.x - prevCOM);
      //zoomOffset = std::min(std::max(-maxDist, zoomOffset + (maxDist/plotWidth/zoomScale)*(event->tfinger.x - prevCOM)), 0.0);
      prevCOM = event->tfinger.x;
      redraw();
    }
    else if(event->type == SDL_MOUSEWHEEL) {
      zoomScale = std::min(std::max(1.0, zoomScale*std::exp(0.25*event->wheel.y/120.0)), maxZoom);
      updateZoomOffset(0);
      redraw();
    }
    else if(onHovered && event->type == SDL_FINGERMOTION && !gui->pressedWidget)
      onHovered((event->tfinger.x - mBounds.left)/mBounds.width());
    else if(onHovered && event->type == SvgGui::LEAVE)
      onHovered(-1);
    else
      return false;
    return true;
  });
}

void TrackPlot::updateZoomOffset(real dx)
{
  real w = plotVsDist ? maxDist : maxTime - minTime;
  zoomOffset = std::min(std::max((1/zoomScale - 1)*w, zoomOffset + dx*w/plotWidth/zoomScale), 0.0);
  if(onPanZoom)
    onPanZoom();
}

real TrackPlot::plotPosToTrackPos(real s)
{
  real w = plotVsDist ? maxDist : maxTime - minTime;
  return s/zoomScale - zoomOffset/w;
}

real TrackPlot::trackPosToPlotPos(real s)
{
  real w = plotVsDist ? maxDist : maxTime - minTime;
  return zoomScale*(s + zoomOffset/w);
}

void TrackPlot::setTrack(const std::vector<Waypoint>& locs)
{
  minAlt = FLT_MAX;
  maxAlt = -FLT_MAX;
  minSpd = FLT_MAX;
  maxSpd = -FLT_MAX;
  minTime = locs.front().loc.time;
  maxTime = locs.back().loc.time;
  maxDist = locs.back().dist;
  altDistPlot.clear();
  altTimePlot.clear();
  spdDistPlot.clear();
  spdTimePlot.clear();
  altDistPlot.addPoint(locs.front().dist, -1000);
  altTimePlot.addPoint(locs.front().loc.time, -1000);
  for(auto& wpt : locs) {
    const Location& tpt = wpt.loc;
    altDistPlot.addPoint(Point(wpt.dist, MapsApp::metricUnits ? tpt.alt : tpt.alt*3.28084));
    altTimePlot.addPoint(Point(tpt.time, MapsApp::metricUnits ? tpt.alt : tpt.alt*3.28084));
    minAlt = std::min(minAlt, tpt.alt);
    maxAlt = std::max(maxAlt, tpt.alt);
    spdDistPlot.addPoint(Point(wpt.dist, MapsApp::metricUnits ? tpt.spd*3600*0.001 : tpt.spd*3600*0.000621371));
    spdTimePlot.addPoint(Point(tpt.time, MapsApp::metricUnits ? tpt.spd*3600*0.001 : tpt.spd*3600*0.000621371));
    minSpd = std::min(minSpd, tpt.spd);
    maxSpd = std::max(maxSpd, tpt.spd);
  }
  altDistPlot.addPoint(locs.back().dist, -1000);
  altTimePlot.addPoint(locs.back().loc.time, -1000);
  if(maxTime - minTime <= 0)
    plotVsDist = true;

  real elev = maxAlt - minAlt;
  minAlt -= 0.05*elev;
  maxAlt += 0.05*elev;
  maxZoom = locs.size()/8;  // min 8 points in view
}

Rect TrackPlot::bounds(SvgPainter* svgp) const
{
  return svgp->p->getTransform().mapRect(mBounds);
}

// should we highlight zoomed region of track on map?
void TrackPlot::draw(SvgPainter* svgp) const
{
  Painter* p = svgp->p;
  int w = mBounds.width() - 4;
  int h = mBounds.height() - 4;
  p->translate(2, 2);
  p->clipRect(Rect::wh(w, h));
  p->fillRect(Rect::wh(w, h), Color::WHITE);

  // labels
  p->setFillBrush(Color::BLACK);
  p->setFontSize(12);
  real labelw = 0;
  int nvert = 5;
  real dh = (maxAlt - minAlt)/nvert;
  for(int ii = 0; ii < nvert; ++ii)
    labelw = std::max(labelw, p->drawText(0, h*(1-real(ii)/nvert), fstring("%.0f", minAlt + ii*dh).c_str()));

  int plotw = w - (labelw + 10);
  int ploth = h - 15;
  int nhorz = 5;
  plotWidth = plotw;
  if(plotVsDist) {
    real xMin = -zoomOffset/1000;
    real xMax = xMin + maxDist/1000/zoomScale;
    real dw = (xMax - xMin)/nhorz;
    int prec = std::max(0, -int(std::floor(std::log10(dw))));
    for(int ii = 0; ii < nhorz; ++ii)
      p->drawText(ii*plotw/nhorz + labelw + 10, h, fstring("%.*f", prec, xMin + ii*dw).c_str());
  }
  else {
    real xMin = minTime + zoomOffset;
    real xMax = xMin + (maxTime - minTime)/zoomScale;
    real dw = (xMax - xMin)/nhorz;
    for(int ii = 0; ii < nhorz; ++ii) {
      real secs = xMin + ii*dw;
      real hrs = std::floor(secs/3600);
      real mins = (secs - hrs*3600)/60;
      p->drawText(ii*plotw/nhorz + labelw + 10, h, fstring("%.0f:%.0f", hrs, mins).c_str());
    }
  }

  // axes
  //drawCheckerboard(p, w, h, 4, 0x18000000);
  p->setStroke(Color::BLUE, 1.5);
  p->setFillBrush(Brush::NONE);
  p->drawLine(Point(labelw + 5, h-15), Point(labelw + 5, 0));
  p->drawLine(Point(labelw + 5, h-15), Point(w, h-15));

  // markers
  //p->setStroke(Color::GREEN, 1.5);
  //for(real x : markers)
  //  p->drawLine(Point(x, 15), Point(x, h));


  // plot
  p->clipRect(Rect::ltrb(labelw + 6, 0, w, h-15));  // clip plot to axes
  p->translate(labelw + 10, 0);
  p->scale(plotVsDist ? plotw/maxDist : plotw/(maxTime - minTime), -ploth/(maxAlt - minAlt));

  p->translate(0, -maxAlt + minAlt);

  p->scale(zoomScale, 1);
  p->translate(zoomOffset, 0);
  if(plotAlt) {
    p->translate(0, -minAlt);
    p->setFillBrush(Color(0, 0, 255, 128));
    p->setStroke(Color::NONE);
    p->drawPath(plotVsDist ? altDistPlot : altTimePlot);
    p->translate(0, minAlt);
  }
  if(plotSpd) {
    p->setFillBrush(Brush::NONE);
    p->setStroke(Color::RED, 2.0);
    p->drawPath(plotVsDist ? spdDistPlot : spdTimePlot);
  }
}


class TrackSliders : public Slider
{
public:
  enum {NO_UPDATE = 0, UPDATE = 1, FORCE_UPDATE = 2};
  TrackSliders(SvgNode* n);
  void setEditMode(bool editmode);
  void setCropHandles(real start, real end, int update);
  std::function<void()> onStartHandleChanged;
  std::function<void()> onEndHandleChanged;

  real startHandlePos = 0;
  real endHandlePos = 0;  // this reflects initial state of widget

private:
  Widget* startHandle;
  Widget* endHandle;
};

TrackSliders::TrackSliders(SvgNode* n) : Slider(n)
{
  startHandle = new Button(containerNode()->selectFirst(".start-handle"));
  endHandle = new Button(containerNode()->selectFirst(".end-handle"));
  //startHandle->setLayoutIsolate(true);
  //endHandle->setLayoutIsolate(true);

  startHandle->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == startHandle) {
      Rect rect = sliderBg->node->bounds();
      real startpos = (event->tfinger.x - rect.left)/rect.width();
      setCropHandles(startpos, std::max(startpos, endHandlePos), UPDATE);
      return true;
    }
    return false;
  });

  endHandle->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == endHandle) {
      Rect rect = sliderBg->node->bounds();
      real endpos = (event->tfinger.x - rect.left)/rect.width();
      setCropHandles(std::min(startHandlePos, endpos), endpos, UPDATE);
      return true;
    }
    return false;
  });

  auto sliderOnApplyLayout = onApplyLayout;
  onApplyLayout = [this, sliderOnApplyLayout](const Rect& src, const Rect& dest){
    sliderOnApplyLayout(src, dest);
    if(src.toSize() != dest.toSize()) {
      Rect rect = sliderBg->node->bounds();
      startHandle->setLayoutTransform(Transform2D().translate(rect.width()*startHandlePos, 0));
      endHandle->setLayoutTransform(Transform2D().translate(rect.width()*endHandlePos, 0));
    }
    return false;  // we do not replace the normal layout (although that should be a no-op)
  };
  setEditMode(false);
}

void TrackSliders::setCropHandles(real start, real end, int update)
{
  start = std::min(std::max(start, 0.0), 1.0);
  end = std::min(std::max(end, 0.0), 1.0);
  Rect rect = sliderBg->node->bounds();
  if(startHandlePos != start || update > 1) {
    startHandlePos = start;
    startHandle->setLayoutTransform(Transform2D().translate(rect.width()*startHandlePos, 0));
    if(update > 0 && onStartHandleChanged)
      onStartHandleChanged();
  }
  if(endHandlePos != end || update > 1) {
    endHandlePos = end;
    endHandle->setLayoutTransform(Transform2D().translate(rect.width()*endHandlePos, 0));
    if(update > 0 && onEndHandleChanged)
      onEndHandleChanged();
  }
}

void TrackSliders::setEditMode(bool editmode)
{
  selectFirst(".start-handle")->setVisible(editmode);
  selectFirst(".end-handle")->setVisible(editmode);
  selectFirst(".slider-handle")->setVisible(!editmode);
}

TrackSliders* createTrackSliders()
{
  static const char* slidersSVG = R"#(
    <g id="slider" class="slider" box-anchor="hfill" layout="box">
      <rect class="slider-bg background" box-anchor="hfill" fill="blue" width="200" height="4"/>
      <g class="slider-handle-container" box-anchor="left">
        <!-- invisible rect to set left edge of box so slider-handle can move freely -->
        <rect width="1" height="16" fill="none"/>
        <g class="start-handle">
          <rect class="slider-handle-outer" x="-6" y="-2" width="12" height="16"/>
          <rect fill="green" x="-4" y="0" width="8" height="12"/>
        </g>

        <g class="slider-handle">
          <rect class="slider-handle-outer" x="-6" y="-2" width="12" height="16"/>
          <rect class="slider-handle-inner" x="-4" y="0" width="8" height="12"/>
        </g>

        <g class="end-handle">
          <rect class="slider-handle-outer" x="-6" y="-2" width="12" height="16"/>
          <rect fill="red" x="-4" y="0" width="8" height="12"/>
        </g>
      </g>
    </g>
  )#";

  return new TrackSliders(loadSVGFragment(slidersSVG));
}

// https://www.topografix.com/gpx_manual.asp
static Waypoint loadWaypoint(const pugi::xml_node& trkpt)
{
  double lat = trkpt.attribute("lat").as_double();
  double lng = trkpt.attribute("lon").as_double();
  //track.emplace_back(lng, lat);
  pugi::xml_node elenode = trkpt.child("ele");
  double ele = atof(elenode.child_value());
  //activeTrack.push_back({track.back(), dist, atof(ele.child_value())});
  double time = 0;
  pugi::xml_node timenode = trkpt.child("time");
  if(timenode) {
    std::tm tmb;
    std::stringstream(timenode.child_value()) >> std::get_time(&tmb, "%Y-%m-%dT%TZ");  //2023-03-31T20:19:15Z
    time = mktime(&tmb);
  }

  Waypoint wpt({time, lat, lng, 0, ele, 0, /*dir*/0, 0, 0, 0},
      trkpt.child("name").child_value(), trkpt.child("desc").child_value());
  pugi::xml_node extnode = trkpt.child("extensions").child("sl:route");
  if(extnode) {
    wpt.visible = extnode.attribute("visible").as_bool(wpt.visible);
    wpt.routed = extnode.attribute("routed").as_bool(wpt.routed);
  }
  return wpt;
}

bool MapsTracks::loadGPX(GpxFile* track, const char* gpxSrc)
{
  pugi::xml_document doc;
  if(gpxSrc)
    doc.load_string(gpxSrc);
  else
    doc.load_file(track->filename.c_str());
  pugi::xml_node gpx = doc.child("gpx");
  if(!gpx)
    return false;
  const char* gpxname = gpx.child("name").child_value();
  const char* gpxdesc = gpx.child("desc").child_value();
  if(gpxname[0]) track->title = gpxname;
  if(gpxdesc[0]) track->desc = gpxdesc;

  pugi::xml_node wpt = gpx.child("wpt");
  while(wpt) {
    track->addWaypoint(loadWaypoint(wpt));
    wpt = wpt.next_sibling("wpt");
  }

  pugi::xml_node rte = gpx.child("rte");
  while(rte) {
    track->routes.emplace_back(rte.child("name").child_value(), rte.child("desc").child_value());
    pugi::xml_node rtept = rte.child("rtept");
    while(rtept) {
      track->routes.back().pts.push_back(loadWaypoint(rtept));
      rtept = rtept.next_sibling("rtept");
    }
    rte = rte.next_sibling("rte");
  }

  pugi::xml_node trk = gpx.child("trk");
  //if(!trk) logMsg("Error loading %s\n", gpxfile);
  while(trk) {
    track->tracks.emplace_back(trk.child("name").child_value(), trk.child("desc").child_value());
    pugi::xml_node trkseg = trk.child("trkseg");
    while(trkseg) {
      //std::vector<LngLat> track;
      pugi::xml_node trkpt = trkseg.child("trkpt");
      while(trkpt) {
        Waypoint pt1 = loadWaypoint(trkpt);
        if(!track->tracks.back().pts.empty()) {
          auto& pt0 = track->tracks.back().pts.back();
          pt1.dist = pt0.dist + 1000*lngLatDist(pt1.lngLat(), pt0.lngLat());
          if(pt1.loc.time > 0)
            pt1.loc.spd = (pt1.dist - pt0.dist)/(pt1.loc.time - pt0.loc.time);
        }
        track->tracks.back().pts.push_back(pt1);
        trkpt = trkpt.next_sibling("trkpt");
      }
      trkseg = trkseg.next_sibling("trkseg");
    }
    trk = trk.next_sibling("trk");
  }
  track->loaded = true;
  return true;
}

static void saveWaypoint(pugi::xml_node trkpt, const Waypoint& wpt)
{
  trkpt.append_attribute("lat").set_value(fstring("%.7f", wpt.loc.lat).c_str());
  trkpt.append_attribute("lon").set_value(fstring("%.7f", wpt.loc.lng).c_str());
  trkpt.append_child("ele").append_child(pugi::node_pcdata).set_value(fstring("%.1f", wpt.loc.alt).c_str());
  if(wpt.loc.time > 0) {
    char timebuf[256];
    time_t t = time_t(wpt.loc.time);
    strftime(timebuf, sizeof(timebuf), "%FT%TZ", gmtime(&t));
    trkpt.append_child("time").append_child(pugi::node_pcdata).set_value(timebuf);
  }
  if(!wpt.name.empty())
    trkpt.append_child("name").append_child(pugi::node_pcdata).set_value(wpt.name.c_str());
  if(!wpt.desc.empty())
    trkpt.append_child("desc").append_child(pugi::node_pcdata).set_value(wpt.desc.c_str());
  if(!wpt.visible || !wpt.routed) {
    pugi::xml_node extnode = trkpt.append_child("extensions").append_child("sl:route");
    extnode.append_attribute("visible").set_value(wpt.visible);
    extnode.append_attribute("routed").set_value(wpt.routed);
  }
}

bool MapsTracks::saveGPX(GpxFile* track)
{
  // saving track
  pugi::xml_document doc;
  pugi::xml_node gpx = doc.append_child("gpx");
  gpx.append_child("name").append_child(pugi::node_pcdata).set_value(track->title.c_str());
  gpx.append_child("desc").append_child(pugi::node_pcdata).set_value(track->desc.c_str());

  for(const Waypoint& wpt : track->waypoints)
    saveWaypoint(gpx.append_child("wpt"), wpt);

  for(const GpxWay& route : track->routes) {
    pugi::xml_node rte = gpx.append_child("rte");
    rte.append_child("name").append_child(pugi::node_pcdata).set_value(route.title.c_str());
    rte.append_child("desc").append_child(pugi::node_pcdata).set_value(route.desc.c_str());
    for(const Waypoint& wpt : route.pts)
      saveWaypoint(rte.append_child("rtept"), wpt);
  }

  for(const GpxWay& t : track->tracks) {
    pugi::xml_node trk = gpx.append_child("trk");
    trk.append_child("name").append_child(pugi::node_pcdata).set_value(t.title.c_str());
    trk.append_child("desc").append_child(pugi::node_pcdata).set_value(t.desc.c_str());
    pugi::xml_node seg = trk.append_child("trkseg");
    for(const Waypoint& wpt : t.pts)
      saveWaypoint(seg.append_child("trkpt"), wpt);
  }
  return doc.save_file(track->filename.c_str(), "  ");
}

//void MapsTracks::tapEvent(LngLat location)
//{
//  if(!drawTrack)
//    return;
//  auto& locs = drawnTrack.locs;
//  double dist = locs.empty() ? 0 : locs.back().dist + 1000*lngLatDist(locs.back().lngLat(), location);
//  double time = 0;
//  locs.push_back({time, location.latitude, location.longitude, 0, /*ele*/0, 0, /*dir*/0, 0, /*spd*/0, 0, dist});
//  showTrack(&drawnTrack);  //, "layers.track.draw.selected-track");
//}

void MapsTracks::updateLocation(const Location& loc)
{
  if(!recordTrack)
    return;
  auto& locs = recordedTrack.tracks.back().pts;
  if(locs.empty()) {
    locs.emplace_back(loc);
    locs.back().dist = 0;
  }
  else {
    auto& prev = locs.back();
    // since altitude is less precise than pos, I don't think we should do sqrt(dist^2 + vert^2)
    double dist = 1000*lngLatDist(loc.lngLat(), prev.lngLat());
    double vert = loc.alt - prev.loc.alt;
    double dt = loc.time - prev.loc.time;
    if(dist > minTrackDist || dt > minTrackTime || vert > minTrackDist) {
      double d0 = prev.dist;
      locs.emplace_back(loc);
      locs.back().dist = d0 + dist;
      if(loc.spd == 0)
        locs.back().loc.spd = dist/dt;
      if(recordedTrack.visible)
        updateTrackMarker(&recordedTrack);  // rebuild marker
      if(activeTrack == &recordedTrack)
        populateStats(&recordedTrack);
      if(loc.time > recordLastSave + 60) {
        saveGPX(&recordedTrack);
        recordLastSave = loc.time;
      }
    }
  }
}

void MapsTracks::updateTrackMarker(GpxFile* track)
{
  if(!track->loaded && !track->filename.empty())
    loadGPX(track);

  std::vector<LngLat> pts;
  GpxWay* way = track->activeWay();
  if(way) {
    for(const Waypoint& wp : way->pts)
      pts.push_back(wp.loc.lngLat());
  }

  if(track->marker <= 0) {
    track->marker = app->map->markerAdd();
    app->map->markerSetStylingFromPath(track->marker, "layers.track.draw.track");  //styling);
  }
  if(pts.size() > 1) {
    app->map->markerSetPolyline(track->marker, pts.data(), pts.size());
    if(!track->style.empty())
      app->map->markerSetProperties(track->marker, {{{"color", track->style}}});
  }
  app->map->markerSetVisible(track->marker, pts.size() > 1);

  for(Waypoint& wp : track->waypoints) {
    if(wp.marker <= 0) {
      wp.marker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(wp.marker, "layers.waypoint.draw.marker");
      app->map->markerSetPoint(wp.marker, wp.loc.lngLat());
      // use marker number as unique id for priority
      app->map->markerSetProperties(wp.marker,
          {{{"name", wp.name}, {"color", track->style}, {"priority", wp.marker}}});
    }
  }
}

void MapsTracks::showTrack(GpxFile* track, bool show)  //, const char* styling)
{
  if(track->marker <= 0) {
    if(!show) return;
    updateTrackMarker(track);
  }
  app->map->markerSetVisible(track->marker, show && track->activeWay() && track->activeWay()->pts.size() > 1);
  for(Waypoint& wp : track->waypoints)
    app->map->markerSetVisible(wp.marker, show);
}

void MapsTracks::setTrackVisible(GpxFile* track, bool visible)
{
  track->visible = visible;
  if(visible)
    app->config["tracks"]["visible"].push_back(track->rowid);
  else
    yamlRemove(app->config["tracks"]["visible"], track->rowid);
  // assume recordedTrack is dirty (should we introduce a Track.dirty flag?)
  if(visible && track == &recordedTrack)
    updateTrackMarker(track);
  showTrack(track, visible);  //, "layers.track.draw.track");
}

Widget* MapsTracks::createTrackEntry(GpxFile* track)
{
  Button* item = createListItem(MapsApp::uiIcon("track"), track->title.c_str(), track->desc.c_str());
  item->onClicked = [=](){
    if(!track->routes.empty() || track->tracks.empty())
      populateWaypoints(track);
    else
      populateStats(track);
  };
  Widget* container = item->selectFirst(".child-container");

  Button* showBtn = createToolbutton(MapsApp::uiIcon("eye"), "Show");
  showBtn->onClicked = [=](){
    setTrackVisible(track, !track->visible);
    showBtn->setChecked(track->visible);
  };
  showBtn->setChecked(track->visible);
  container->addWidget(showBtn);

  ColorPicker* colorBtn = createColorPicker(app->markerColors, parseColor(track->style, Color::BLUE));
  colorBtn->onColor = [this, track](Color color){
    char buff[64];
    SvgWriter::serializeColor(buff, color);
    track->style = buff;
    if(track->marker > 0) {
      app->map->markerSetProperties(track->marker, {{{"color", buff}}});
      app->map->markerSetStylingFromPath(track->marker, "layers.track.draw.track");  // force refresh
    }
    if(track->rowid >= 0) {
      DB_exec(app->bkmkDB, "UPDATE tracks SET style = ? WHERE rowid = ?;", NULL, [&](sqlite3_stmt* stmt1){
        sqlite3_bind_text(stmt1, 1, buff, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt1, 2, track->rowid);
      });
    }
  };
  container->addWidget(colorBtn);

  if(track->rowid >= 0) {
    Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowMenu->addItem(track->archived ? "Unarchive" : "Archive", [=](){
      //std::string q1 = fstring("UPDATE lists_state SET order = (SELECT COUNT(1) FROM lists WHERE archived = %d) WHERE list_id = ?;", archived ? 0 : 1);
      //DB_exec(app->bkmkDB, q1.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
      std::string q2 = fstring("UPDATE tracks SET archived = %d WHERE rowid = ?;", track->archived ? 0 : 1);
      DB_exec(app->bkmkDB, q2.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, track->rowid); });
      track->archived = !track->archived;
      if(!track->archived) tracksDirty = true;
      app->gui->deleteWidget(item);
    });

    overflowMenu->addItem("Delete", [=](){
      DB_exec(app->bkmkDB, "DELETE FROM tracks WHERE rowid = ?", NULL, [=](sqlite3_stmt* stmt){
        sqlite3_bind_int(stmt, 1, track->rowid);
      });
      for(auto it = tracks.begin(); it != tracks.end(); ++it) {
        if(it->rowid == track->rowid) {
          app->map->markerRemove(track->marker);
          yamlRemove(app->config["tracks"]["visible"], track->rowid);
          tracks.erase(it);
          break;
        }
      }
      app->gui->deleteWidget(item);  //populateTracks(archived);  // refresh
    });
    overflowBtn->setMenu(overflowMenu);
    container->addWidget(overflowBtn);
  }
  return item;
}

void MapsTracks::loadTracks(bool archived)
{
  const char* query = "SELECT rowid, title, filename, strftime('%Y-%m-%d', timestamp, 'unixepoch'), style FROM tracks WHERE archived = ?;";
  DB_exec(app->bkmkDB, query, [this, archived](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    const char* title = (const char*)(sqlite3_column_text(stmt, 1));
    const char* filename = (const char*)(sqlite3_column_text(stmt, 2));
    const char* date = (const char*)(sqlite3_column_text(stmt, 3));
    const char* style = (const char*)(sqlite3_column_text(stmt, 4));
    tracks.emplace_back(title, date, filename);
    tracks.back().rowid = rowid;
    tracks.back().style = style ? style : "";
    tracks.back().archived = archived;
  }, [=](sqlite3_stmt* stmt){
    sqlite3_bind_int(stmt, 1, archived ? 1 : 0);
  });
}

void MapsTracks::populateTracks(bool archived)
{
  tracksDirty = false;
  Widget* content = archived ? archivedContent : tracksContent;
  app->gui->deleteContents(content, ".listitem");

  if(archived && !archiveLoaded) {
    loadTracks(true);
    archiveLoaded = true;
  }

  if(recordTrack && !archived)
    content->addWidget(createTrackEntry(&recordedTrack));
  for(GpxFile& track : tracks) {
    if(track.archived == archived)
      content->addWidget(createTrackEntry(&track));
  }
  if(!archived) {
    Button* item = createListItem(MapsApp::uiIcon("archive"), "Archived Tracks");
    item->onClicked = [this](){ app->showPanel(archivedPanel, true);  populateTracks(true); };
    content->addWidget(item);
  }
}

void MapsTracks::populateStats(GpxFile* track)
{
  app->showPanel(statsPanel, true);
  statsPanel->selectFirst(".panel-title")->setText(track->title.c_str());
  showTrack(track, true);

  bool isRecTrack = track == &recordedTrack;
  pauseRecordBtn->setVisible(isRecTrack);
  stopRecordBtn->setVisible(isRecTrack);
  if(!isRecTrack)
    app->map->markerSetStylingFromPath(track->marker, "layers.selected-track.draw.track");

  auto& locs = track->activeWay()->pts;
  if(!origLocs.empty())
    locs.front().dist = 0;
  // how to calculate max speed?
  double trackDist = 0, trackAscent = 0, trackDescent = 0, ascentTime = 0, descentTime = 0, movingTime = 0;
  double currSpeed = 0, maxSpeed = 0;
  LngLat minLngLat(locs.front().lngLat()), maxLngLat(locs.front().lngLat());
  for(size_t ii = 1; ii < locs.size(); ++ii) {
    const Location& prev = locs[ii-1].loc;
    Location& loc = locs[ii].loc;
    double dist = 1000*lngLatDist(loc.lngLat(), prev.lngLat());
    double vert = loc.alt - prev.alt;
    double dt = loc.time - prev.time;

    if(dist > minTrackDist || vert > minTrackDist)
      movingTime += dt;

    if(dt > 0 && loc.poserr < 100) {
      // single pole IIR low pass filter for speed
      float a = std::exp(-dt*speedInvTau);
      currSpeed = a*currSpeed + (1-a)*dist/dt;
      maxSpeed = std::max(currSpeed, maxSpeed);
    }

    trackDist += dist;
    trackAscent += std::max(0.0, vert);
    trackDescent += std::min(0.0, vert);
    ascentTime += vert > 0 ? dt : 0;
    descentTime += vert < 0 ? dt : 0;

    minLngLat.longitude = std::min(minLngLat.longitude, loc.lng);
    minLngLat.latitude = std::min(minLngLat.latitude, loc.lat);
    maxLngLat.longitude = std::max(maxLngLat.longitude, loc.lng);
    maxLngLat.latitude = std::max(maxLngLat.latitude, loc.lat);

    if(!origLocs.empty())  // track has been modified - recalc distances
      locs[ii].dist = trackDist;
  }

  if(activeTrack != track) {
    if(!app->map->lngLatToScreenPosition(minLngLat.longitude, minLngLat.latitude)
        || !app->map->lngLatToScreenPosition(maxLngLat.longitude, maxLngLat.latitude)) {
      app->map->setCameraPositionEased(app->map->getEnclosingCameraPosition(minLngLat, maxLngLat, {32}), 0.5f);
    }
    activeTrack = track;
  }
  trackPlot->setTrack(locs);

  const Location& loc = locs.back().loc;
  //std::string posStr = fstring("%.6f, %.6f", loc.lat, loc.lng);
  //statsContent->selectFirst(".track-position")->setText(posStr.c_str());
  statsContent->selectFirst(".track-latitude")->setText(fstring("%.6f", loc.lat).c_str());
  statsContent->selectFirst(".track-longitude")->setText(fstring("%.6f", loc.lng).c_str());

  std::string altStr = app->metricUnits ? fstring("%.0f m", loc.alt) : fstring("%.0f ft", loc.alt*3.28084);
  statsContent->selectFirst(".track-altitude")->setText(altStr.c_str());

  // m/s -> kph or mph
  std::string speedStr = app->metricUnits ? fstring("%.2f km/h", currSpeed*3.6) : fstring("%.2f mph", currSpeed*2.23694);
  statsContent->selectFirst(".track-speed")->setText(speedStr.c_str());

  double ttot = locs.back().loc.time - locs.front().loc.time;
  int hours = int(ttot/3600);
  int mins = int((ttot - hours*3600)/60);
  int secs = int(ttot - hours*3600 - mins*60);
  statsContent->selectFirst(".track-time")->setText(fstring("%dh %dm %ds", hours, mins, secs).c_str());

  static const char* notime = u8"\u2014";  // emdash

  ttot = movingTime;
  hours = int(ttot/3600);
  mins = int((ttot - hours*3600)/60);
  secs = int(ttot - hours*3600 - mins*60);
  statsContent->selectFirst(".track-moving-time")->setText(fstring("%dh %dm %02ds", hours, mins, secs).c_str());

  double distUser = app->metricUnits ? trackDist/1000 : trackDist*0.000621371;
  std::string distStr = fstring(app->metricUnits ? "%.2f km" : "%.2f mi", distUser);
  statsContent->selectFirst(".track-dist")->setText(distStr.c_str());

  std::string avgSpeedStr = fstring(app->metricUnits ? "%.2f km/h" : "%.2f mph", distUser/(movingTime/3600));
  statsContent->selectFirst(".track-avg-speed")->setText(ttot > 0 ? avgSpeedStr.c_str() : notime);

  std::string ascentStr = app->metricUnits ? fstring("%.0f m", trackAscent) : fstring("%.0f ft", trackAscent*3.28084);
  statsContent->selectFirst(".track-ascent")->setText(ascentStr.c_str());

  std::string ascentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackAscent/(ascentTime/3600))
      : fstring("%.0f ft/h", (trackAscent*3.28084)/(ascentTime/3600));
  statsContent->selectFirst(".track-ascent-speed")->setText(ascentTime > 0 ? ascentSpdStr.c_str() : notime);

  std::string descentStr = app->metricUnits ? fstring("%.0f m", trackDescent) : fstring("%.0f ft", trackDescent*3.28084);
  statsContent->selectFirst(".track-descent")->setText(descentStr.c_str());

  std::string descentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackDescent/(descentTime/3600))
      : fstring("%.0f ft/h", (trackDescent*3.28084)/(descentTime/3600));
  statsContent->selectFirst(".track-descent-speed")->setText(descentTime > 0 ? descentSpdStr.c_str() : notime);
}

void MapsTracks::createRoute(GpxFile* track)
{
  track->routes.clear();
  track->modified = true;
  if(routeMode == "direct") {
    track->routes.emplace_back();
    for(Waypoint& wp : track->waypoints) {
      if(wp.routed)
        track->routes.back().pts.push_back(wp);
    }
    updateTrackMarker(track);
  }
  else {
    std::vector<LngLat> pts;
    for(Waypoint& wp : track->waypoints) {
      if(wp.routed)
        pts.push_back(wp.loc.lngLat());
    }
    if(!pts.empty())
      app->pluginManager->jsRoute(pluginFn, routeMode, pts);
  }
}

// supporting multiple routes: std::list<std::vector<Waypoint>> altRoutes;
void MapsTracks::addRoute(std::vector<Waypoint>&& route)
{
  activeTrack->routes.emplace_back();
  activeTrack->routes.back().pts = std::move(route);
  activeTrack->modified = true;
  // update track marker
  updateTrackMarker(activeTrack);
}

static std::vector<Waypoint>::iterator findWaypoint(GpxFile* track, const std::string& uid)
{
  auto it = track->waypoints.begin();
  while(it != track->waypoints.end() && it->uid != uid) { ++it; }
  return it;
}

void MapsTracks::removeWaypoint(const std::string& uid)
{
  if(!activeTrack) return;  // should never happen
  auto it = findWaypoint(activeTrack, uid);
  if(it == activeTrack->waypoints.end()) return;  // also should never happen
  bool routed = it->routed;
  if(it->marker > 0)
    app->map->markerRemove(it->marker);
  activeTrack->waypoints.erase(it);
  activeTrack->modified = true;
  if(routed)
    createRoute(activeTrack);
  wayptContent->deleteItem(uid);
}

// cut and paste from bookmarks
void MapsTracks::setPlaceInfoSection(const Waypoint& wpt)
{
  Widget* section = createColumn();
  section->node->setAttribute("box-anchor", "hfill");
  TextBox* noteText = new TextBox(loadSVGFragment(
      R"(<text class="note-text weak" box-anchor="left" margin="0 10" font-size="12"></text>)"));
  noteText->setText(wpt.desc.c_str());
  noteText->setText(SvgPainter::breakText(static_cast<SvgText*>(noteText->node), 250).c_str());

  auto editToolbar = createToolbar();
  auto titleEdit = createTextEdit();
  auto noteEdit = createTextEdit();
  auto acceptNoteBtn = createToolbutton(MapsApp::uiIcon("accept"));
  auto cancelNoteBtn = createToolbutton(MapsApp::uiIcon("cancel"));

  titleEdit->setText(wpt.name.c_str());
  noteEdit->setText(wpt.desc.c_str());

  Widget* editContent = createColumn();
  editContent->addWidget(createTitledRow("Name", titleEdit));
  editContent->addWidget(createTitledRow("Note", noteEdit));
  editToolbar->addWidget(acceptNoteBtn);
  editToolbar->addWidget(cancelNoteBtn);
  editContent->addWidget(editToolbar);
  editContent->setVisible(false);

  std::string uid = wpt.uid;
  acceptNoteBtn->onClicked = [=](){
    auto it = findWaypoint(activeTrack, uid);
    it->name = titleEdit->text();
    it->desc = noteEdit->text();

    // update title
    SvgText* titlenode = static_cast<SvgText*>(app->infoContent->containerNode()->selectFirst(".title-text"));
    titlenode->setText(titleEdit->text().c_str());

    noteText->setText(noteEdit->text().c_str());
    editContent->setVisible(false);
    noteText->setVisible(true);
    waypointsDirty = true;
  };

  cancelNoteBtn->onClicked = [=](){
    editContent->setVisible(false);
    noteText->setVisible(true);
  };

  Widget* toolRow = createRow();
  Button* chooseListBtn = createToolbutton(MapsApp::uiIcon("pin"), activeTrack->title.c_str(), true);
  Button* removeBtn = createToolbutton(MapsApp::uiIcon("discard"), "Delete");
  Button* addNoteBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit");

  removeBtn->onClicked = [=](){
    removeWaypoint(uid);
    section->setVisible(false);
  };

  addNoteBtn->onClicked = [=](){
    editContent->setVisible(true);
    app->gui->setFocused(noteEdit);
  };

  toolRow->addWidget(chooseListBtn);
  toolRow->addWidget(createStretch());
  toolRow->addWidget(removeBtn);
  toolRow->addWidget(addNoteBtn);

  section->addWidget(toolRow);
  section->addWidget(noteText);
  section->addWidget(editContent);

  //return section;
  app->infoContent->selectFirst(".waypt-section")->addWidget(section);
}

void MapsTracks::addWaypointItem(Waypoint& wp)
{
  std::string uid = wp.uid;

  Button* item = createListItem(MapsApp::uiIcon("pin"), wp.name.c_str(), wp.desc.c_str());
  Widget* container = item->selectFirst(".child-container");
  Button* showBtn = createToolbutton(MapsApp::uiIcon("eye"), "Show");
  Button* routeBtn = createToolbutton(MapsApp::uiIcon("track"), "Route");
  Button* discardBtn = createToolbutton(MapsApp::uiIcon("discard"), "Remove");
  container->addWidget(showBtn);
  container->addWidget(routeBtn);
  container->addWidget(discardBtn);

  item->onClicked = [=](){
    auto it = findWaypoint(activeTrack, uid);
    app->setPickResult(it->lngLat(), it->name, "");
    setPlaceInfoSection(*it);
  };

  showBtn->setChecked(wp.visible);
  showBtn->onClicked = [=](){
    auto it = findWaypoint(activeTrack, uid);
    it->visible = !it->visible;
    showBtn->setChecked(it->visible);
    activeTrack->modified = true;
    //app->map->markerSetStylingFromPath(wp.marker,
    //    wp.visible ? "layers.waypoint.draw.marker" : "layers.waypoint-dot.draw.marker");
    if(!showAllWaypts && !it->visible)
      wayptContent->deleteItem(uid);
  };

  routeBtn->setChecked(wp.routed);
  routeBtn->onClicked = [=](){
    auto it = findWaypoint(activeTrack, uid);
    it->routed = !it->routed;
    routeBtn->setChecked(it->routed);
    createRoute(activeTrack);
  };

  discardBtn->onClicked = [=](){ removeWaypoint(uid); };

  wayptContent->addItem(uid, item);

  wayptContent->onReorder = [=](std::string key, std::string next){
    auto itsrc = findWaypoint(activeTrack, key);
    auto itnext = next.empty() ? activeTrack->waypoints.end() : findWaypoint(activeTrack, next);
    bool routed = itsrc->routed;
    if(itsrc < itnext)  // moved down
      std::rotate(itsrc, itsrc+1, itnext);
    else  // moved up
      std::rotate(itnext, itsrc, itsrc+1);
    if(routed)
      createRoute(activeTrack);
  };
}

void MapsTracks::populateWaypoints(GpxFile* track)
{
  waypointsDirty = false;
  app->showPanel(wayptPanel, true);
  wayptPanel->selectFirst(".panel-title")->setText(track->title.c_str());
  activeTrack = track;
  showTrack(track, true);
  directRoutePreview = true;
  app->crossHair->setVisible(true);

  wayptContent->clear();
  for(Waypoint& wp : track->waypoints) {
    if(wp.visible || showAllWaypts)
      addWaypointItem(wp);
  }
}

#include "util/mapProjection.h"

void MapsTracks::onMapEvent(MapEvent_t event)
{
  if(!activeTrack)
    return;
  if(event == LOC_UPDATE)
    updateLocation(app->currLocation);
  else if(event == SUSPEND && activeTrack->modified)
    activeTrack->modified = !saveGPX(activeTrack);
  if(event != MAP_CHANGE)
    return;

  // update polyline marker in direct mode
  if(directRoutePreview && routeMode == "direct" && !activeTrack->waypoints.empty()) {
    std::vector<LngLat> pts = {activeTrack->waypoints.back().lngLat(), app->getMapCenter()};
    double pix = lngLatDist(pts[0], pts[1])*1000/MapProjection::metersPerPixelAtZoom(app->map->getZoom());
    if(previewMarker <= 0) {
      previewMarker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(previewMarker, "layers.track.draw.track");  //styling);
      // geometry must be set before properties for new marker
      app->map->markerSetPolyline(previewMarker, pts.data(), 2);
      app->map->markerSetProperties(previewMarker, {{{"color", "red"}}});
      app->map->markerSetVisible(previewMarker, pix > 2);
    }
    else if(!(pts[0] == previewRoute[0] && pts[1] == previewRoute[1])) {
      app->map->markerSetPolyline(previewMarker, pts.data(), 2);
      app->map->markerSetVisible(previewMarker, pix > 2);  //activeTrack->routes.front().pts.back().lngLat()
    }
    previewRoute = pts;
  }

  if(app->pickedMarkerId > 0) {
    for(auto& wpt : activeTrack->waypoints) {
      if(wpt.marker == app->pickedMarkerId) {
        app->setPickResult(wpt.lngLat(), wpt.name, "");
        setPlaceInfoSection(wpt);
        app->pickedMarkerId = 0;
      }
    }
  }
}

void MapsTracks::setRouteMode(const std::string& mode)
{
  if(routeMode == mode) return;
  routeMode = mode;
  auto parts = splitStr<std::vector>(mode.c_str(), '-');
  const char* icon = "segment";
  if(parts[0] == "walk") icon = "walk";
  else if(parts[0] == "bike") icon = "bike";
  else if(parts[0] == "drive") icon = "car";
  routeModeBtn->setIcon(MapsApp::uiIcon(icon));
  if(activeTrack)
    createRoute(activeTrack);
};

void MapsTracks::addPlaceActions(Toolbar* tb)
{
  if(activeTrack) {
    Button* addWptBtn = createToolbutton(MapsApp::uiIcon("directions"), "Add waypoint");
    addWptBtn->onClicked = [=](){
      activeTrack->addWaypoint({app->pickResultCoord, app->pickResultName});  //++activeTrack->wayPtSerial?
      activeTrack->modified = true;
      addWaypointItem(activeTrack->waypoints.back());
      if(activeTrack->waypoints.back().routed)
        createRoute(activeTrack);
    };
    tb->addWidget(addWptBtn);
  }
  else {
    Button* routeBtn = createToolbutton(MapsApp::uiIcon("directions"), "Directions");
    routeBtn->onClicked = [=](){
      LngLat r1 = app->currLocation.lngLat(), r2 = app->pickResultCoord;
      double km = lngLatDist(r1, r2);
      setRouteMode(km < 10 ? "walk" : km < 100 ? "bike" : "drive");
      navRoute.waypoints.clear();
      navRoute.addWaypoint({r1, "Current location"});
      navRoute.addWaypoint({r2, app->pickResultName});
      navRoute.modified = true;
      populateWaypoints(&navRoute);
      createRoute(&navRoute);
    };
    tb->addWidget(routeBtn);
  }
}

static Widget* createStatsRow(std::vector<const char*> items)  // const char* title1, const char* class1, const char* title2, const char* class2)
{
  static const char* statBlockProtoSVG = R"(
    <g layout="box">
      <rect fill="none" width="150" height="50"/>
      <text class="title-text weak" box-anchor="left top" margin="0 0 0 10" font-size="12"></text>
      <text class="stat-text" box-anchor="left" margin="0 0 0 16"></text>
    </g>
  )";
  std::unique_ptr<SvgNode> statBlockProto;
  if(!statBlockProto)
    statBlockProto.reset(loadSVGFragment(statBlockProtoSVG));

  Widget* row = createRow();
  for(size_t ii = 0; ii+1 < items.size(); ii += 2) {
    Widget* block = new Widget(statBlockProto->clone());
    block->selectFirst(".title-text")->setText(items[ii]);
    block->selectFirst(".stat-text")->node->addClass(items[ii+1]);
    row->addWidget(block);
  }
  return row;
}

static Waypoint interpLoc(const Waypoint& a, const Waypoint& b, double f)
{
  float g = float(f);
  return Waypoint(Location{
      f*b.loc.time   + (1-f)*a.loc.time,
      f*b.loc.lat    + (1-f)*a.loc.lat,
      f*b.loc.lng    + (1-f)*a.loc.lng,
      g*b.loc.poserr + (1-g)*a.loc.poserr,
      f*b.loc.alt    + (1-f)*a.loc.alt,
      g*b.loc.alterr + (1-g)*a.loc.alterr,
      g*b.loc.dir    + (1-g)*a.loc.dir,
      g*b.loc.direrr + (1-g)*a.loc.direrr,
      g*b.loc.spd    + (1-g)*a.loc.spd,
      g*b.loc.spderr + (1-g)*a.loc.spderr},
      f*b.dist + (1-f)*a.dist);
}

static Waypoint interpTrackDist(const std::vector<Waypoint>& locs, double s, size_t* idxout)
{
  double sd = s*locs.back().dist;
  size_t jj = 0;
  while(locs[jj].dist < sd) ++jj;
  if(idxout) *idxout = jj;
  double f = (sd - locs[jj-1].dist)/(locs[jj].dist - locs[jj-1].dist);
  return interpLoc(locs[jj-1], locs[jj], f);
}

static Waypoint interpTrackTime(const std::vector<Waypoint>& locs, double s, size_t* idxout)
{
  double st = s*locs.back().loc.time + (1-s)*locs.front().loc.time;
  size_t jj = 0;
  while(locs[jj].loc.time < st) ++jj;
  if(idxout) *idxout = jj;
  double f = (st - locs[jj-1].loc.time)/(locs[jj].loc.time - locs[jj-1].loc.time);
  return interpLoc(locs[jj-1], locs[jj], f);
}

Waypoint MapsTracks::interpTrack(const std::vector<Waypoint>& locs, double s, size_t* idxout)
{
  s = std::min(std::max(s, 0.0), 1.0);
  return trackPlot->plotVsDist ? interpTrackDist(locs, s, idxout) : interpTrackTime(locs, s, idxout);
}

Button* MapsTracks::createPanel()
{
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS tracks(title TEXT, filename TEXT, style TEXT,"
      " timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)), archived INTEGER DEFAULT 0);");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS tracks_state(track_id INTEGER, ordering INTEGER, visible INTEGER);");

  // Stats panel
  statsContent = createColumn();

  Widget* saveTrackContent = createColumn();
  TextEdit* saveTitle = createTextEdit();
  TextEdit* savePath = createTextEdit();
  CheckBox* replaceTrackCb = createCheckBox("Replace track", false);
  Button* saveTrackBtn = createPushbutton("Apply");
  Button* restoreTrackBtn = createPushbutton("Cancel");

  savePath->onChanged = [=](const char* path){
    bool newfile = path != activeTrack->filename;
    replaceTrackCb->setEnabled(newfile);
    if(newfile) {
      bool pathexists = FSPath(path).exists();
      //fileExistsMsg->setVisible(pathexists);
      saveTrackBtn->setEnabled(!pathexists);
    }
    else
      replaceTrackCb->setChecked(false);
  };

  saveTrackContent->addWidget(createTitledRow("Title", saveTitle));
  saveTrackContent->addWidget(createTitledRow("File", savePath));
  saveTrackContent->addWidget(replaceTrackCb);
  saveTrackContent->addWidget(createTitledRow(NULL, saveTrackBtn, restoreTrackBtn));
  saveTrackContent->setVisible(false);
  statsContent->addWidget(saveTrackContent);

  // bearing? direction (of travel)?
  statsContent->addWidget(createStatsRow({"Latitude", "track-latitude", "Longitude", "track-longitude"}));
  statsContent->addWidget(createStatsRow({"Altitude", "track-altitude", "Speed", "track-speed"}));
  //statsContent->addWidget(createStatsRow({"Position", "track-position", "Altitude", "track-altitude", "Speed", "track-speed"}));
  statsContent->addWidget(createStatsRow({"Total time", "track-time", "Moving time", "track-moving-time"}));
  statsContent->addWidget(createStatsRow({"Distance", "track-dist", "Avg speed", "track-avg-speed"}));
  statsContent->addWidget(createStatsRow({"Ascent", "track-ascent", "Descent", "track-descent"}));
  statsContent->addWidget(createStatsRow({"Ascent speed", "track-ascent-speed", "Descent speed", "track-descent-speed"}));

  trackPlot = new TrackPlot();
  trackPlot->node->setAttribute("box-anchor", "hfill");
  trackPlot->setMargins(1, 5);

  statsContent->addWidget(trackPlot);

  TrackSliders* trackSliders = createTrackSliders();
  statsContent->addWidget(trackSliders);

  trackSliders->onStartHandleChanged = [=](){
    cropStart = trackPlot->plotPosToTrackPos(trackSliders->startHandlePos);
    Waypoint startloc = interpTrack(activeTrack->tracks.front().pts, cropStart);
    if(trackStartMarker == 0) {
      trackStartMarker = app->map->markerAdd();
      app->map->markerSetPoint(trackStartMarker, startloc.lngLat());  // must set geometry before properties
      app->map->markerSetProperties(trackStartMarker, {{{"color", "#008000"}}});
      app->map->markerSetStylingFromPath(trackStartMarker, "layers.track-marker.draw.marker");
    } else {
      app->map->markerSetVisible(trackStartMarker, true);
      app->map->markerSetPoint(trackStartMarker, startloc.lngLat());
    }
  };

  trackSliders->onEndHandleChanged = [=](){
    cropEnd = trackPlot->plotPosToTrackPos(trackSliders->endHandlePos);
    Waypoint endloc = interpTrack(activeTrack->tracks.front().pts, cropEnd);
    if(trackEndMarker == 0) {
      trackEndMarker = app->map->markerAdd();
      app->map->markerSetPoint(trackEndMarker, endloc.lngLat());  // must set geometry before properties
      app->map->markerSetProperties(trackEndMarker, {{{"color", "#FF0000"}}});
      app->map->markerSetStylingFromPath(trackEndMarker, "layers.track-marker.draw.marker");
    } else {
      app->map->markerSetVisible(trackEndMarker, true);
      app->map->markerSetPoint(trackEndMarker, endloc.lngLat());
    }
  };

  Button* createBkmkBtn = createToolbutton(NULL, "Create bookmark", true);
  createBkmkBtn->onClicked = [=](){
    Waypoint loc = interpTrack(activeTrack->tracks.front().pts, trackSliders->value());
    // TODO: how to open with add bookmark section expanded?  pass flag forwarded to getPlaceInfoSubSection()?
    app->setPickResult(loc.lngLat(), activeTrack->title + " waypoint", "");
  };

  Button* cropTrackBtn = createToolbutton(NULL, "Crop to segment", true);
  cropTrackBtn->onClicked = [=](){
    if(origLocs.empty()) origLocs = activeTrack->tracks.front().pts;
    auto& locs = activeTrack->tracks.front().pts;
    std::vector<Waypoint> newlocs;
    size_t startidx, endidx;
    newlocs.push_back(interpTrack(locs, cropStart, &startidx));
    auto endloc = interpTrack(locs, cropEnd, &endidx);
    newlocs.insert(newlocs.end(), locs.begin() + startidx, locs.begin() + endidx);
    newlocs.push_back(endloc);
    activeTrack->tracks.front().pts.swap(newlocs);
    trackSliders->setCropHandles(0, 1, TrackSliders::FORCE_UPDATE);
    updateTrackMarker(activeTrack);  // rebuild marker
    populateStats(activeTrack);
  };

  Button* appendTrackBtn = createToolbutton(NULL, "Append track", true);
  appendTrackBtn->onClicked = [this](){
    if(!selectTrackDialog) {
      selectTrackDialog.reset(createSelectDialog("Choose Track", MapsApp::uiIcon("track")));
      selectTrackDialog->onSelected = [this](int idx){
        auto* way = tracks[idx].activeWay();
        if(!way) return;
        if(origLocs.empty()) origLocs = activeTrack->tracks.front().pts;
        auto& locs = activeTrack->tracks.front().pts;
        locs.insert(locs.end(), way->pts.begin(), way->pts.end());
        updateTrackMarker(activeTrack);  // rebuild marker
        populateStats(activeTrack);
      };
    }
    selectTrackDialog->addItems({});
    for(auto& track : tracks) {
      if(!track.archived)
        selectTrackDialog->addItems({track.title});
    }
    MapsApp::gui->showModal(selectTrackDialog.get(), MapsApp::gui->windows.front()->modalOrSelf());
  };

  Button* moreTrackOptionsBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More options");
  Menu* trackPlotOverflow = createMenu(Menu::VERT_LEFT);
  moreTrackOptionsBtn->setMenu(trackPlotOverflow);

  trackPlotOverflow->addItem("Reverse Track", [this](){
    if(origLocs.empty()) origLocs = activeTrack->tracks.front().pts;
    auto& locs = activeTrack->tracks.front().pts;
    std::reverse(locs.begin(), locs.end());
    updateTrackMarker(activeTrack);  // rebuild marker
    populateStats(activeTrack);
  });

  trackPlotOverflow->addItem("Delete Segment", [=](){
    if(origLocs.empty()) origLocs = activeTrack->tracks.front().pts;
    auto& locs = activeTrack->tracks.front().pts;
    std::vector<Waypoint> newlocs;
    size_t startidx, endidx;
    auto startloc = interpTrack(locs, cropStart, &startidx);
    auto endloc = interpTrack(locs, cropEnd, &endidx);
    newlocs.insert(newlocs.end(), locs.begin(), locs.begin() + startidx);
    newlocs.push_back(startloc);
    newlocs.push_back(endloc);
    newlocs.insert(newlocs.end(), locs.begin() + endidx, locs.end());
    locs.swap(newlocs);
    trackSliders->setCropHandles(0, 1, TrackSliders::FORCE_UPDATE);
    updateTrackMarker(activeTrack);  // rebuild marker
    populateStats(activeTrack);
  });

  Toolbar* editTrackTb = createToolbar();
  editTrackTb->addWidget(cropTrackBtn);
  editTrackTb->addWidget(appendTrackBtn);
  editTrackTb->addWidget(moreTrackOptionsBtn);
  editTrackTb->setVisible(false);
  statsContent->addWidget(editTrackTb);

  Toolbar* trackOptionsTb = createToolbar();
  trackOptionsTb->addWidget(createBkmkBtn);
  trackOptionsTb->setVisible(false);
  statsContent->addWidget(trackOptionsTb);

  Button* editTrackBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit Track");
  pauseRecordBtn = createToolbutton(MapsApp::uiIcon("pause"), "Pause");
  stopRecordBtn = createToolbutton(MapsApp::uiIcon("stop"), "Stop");

  auto setTrackEdit = [=](bool show){
    //editTrackBtn->setChecked(show);
    //saveTrackContent->setVisible(show);
    editTrackTb->setVisible(show);
    trackOptionsTb->setVisible(show);
    trackSliders->setEditMode(show);
    if(show)
      trackSliders->setCropHandles(0, 1, TrackSliders::FORCE_UPDATE);
    else {
      app->map->markerSetVisible(trackStartMarker, false);
      app->map->markerSetVisible(trackEndMarker, false);
      app->map->markerSetVisible(trackHoverMarker, true);
      if(!origLocs.empty()) {
        activeTrack->tracks.front().pts = std::move(origLocs);
        updateTrackMarker(activeTrack);  // rebuild marker
        populateStats(activeTrack);
      }
    }
  };

  pauseRecordBtn->onClicked = [=](){
    recordTrack = !recordTrack;
    pauseRecordBtn->setChecked(!recordTrack);  // should actually toggle between play and pause icons
    // show/hide track editing controls
    if(recordTrack && editTrackTb->isVisible())
      setTrackEdit(false);
  };

  stopRecordBtn->onClicked = [=](){
    saveGPX(&recordedTrack);
    const char* query = "INSERT INTO tracks (title,filename) VALUES (?,?);";
    DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, recordedTrack.title.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, recordedTrack.filename.c_str(), -1, SQLITE_TRANSIENT);
    });
    recordedTrack.rowid = sqlite3_last_insert_rowid(app->bkmkDB);
    tracks.push_back(std::move(recordedTrack));
    recordedTrack = GpxFile();
    recordTrack = false;
    tracksDirty = true;
    pauseRecordBtn->setChecked(false);
    pauseRecordBtn->setVisible(false);
    stopRecordBtn->setVisible(false);
    populateStats(&tracks.back());
  };

  editTrackBtn->onClicked = [=](){
    bool show = !editTrackBtn->isChecked();
    editTrackBtn->setChecked(show);
    saveTrackContent->setVisible(show);
    if(!recordTrack)
      setTrackEdit(show);
    if(show) {
      saveTitle->setText(activeTrack->title.c_str());
      savePath->setText(activeTrack->filename.c_str());
      saveTrackBtn->setEnabled(false);
    }
  };
  restoreTrackBtn->onClicked = [=](){ editTrackBtn->onClicked(); };

  saveTrackBtn->onClicked = [=](){
    std::string prevFile = activeTrack->filename;
    activeTrack->title = saveTitle->text();
    activeTrack->filename = savePath->text();
    if(saveGPX(activeTrack)) {
      bool replace = replaceTrackCb->isChecked();
      if(activeTrack->rowid >= 0) {
        const char* query = replace ? "UPDATE tracks SET title = ?, filename = ? WHERE rowid = ?;"
            : "INSERT INTO tracks (title,filename) VALUES (?,?);";
        DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
          sqlite3_bind_text(stmt, 1, activeTrack->title.c_str(), -1, SQLITE_TRANSIENT);
          sqlite3_bind_text(stmt, 2, activeTrack->filename.c_str(), -1, SQLITE_TRANSIENT);
          if(replace)
            sqlite3_bind_int(stmt, 3, activeTrack->rowid);
        });
        if(!replace)
          activeTrack->rowid = sqlite3_last_insert_rowid(app->bkmkDB);
      }
      if(replace && !prevFile.empty() && prevFile != activeTrack->filename)
        removeFile(prevFile);
      origLocs.clear();
      editTrackBtn->onClicked();  // close edit track view
    }
    else
      activeTrack->filename = prevFile;
    tracksDirty = true;
  };

  auto hoverFn = [this](real s){
    if(s < 0 || s > 1 || !activeTrack) {
      app->map->markerSetVisible(trackHoverMarker, false);
      return;
    }
    Waypoint loc = interpTrack(activeTrack->activeWay()->pts, s);
    if(trackHoverMarker == 0) {
      trackHoverMarker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(trackHoverMarker, "layers.track-marker.draw.marker");
    }
    app->map->markerSetVisible(trackHoverMarker, true);
    app->map->markerSetPoint(trackHoverMarker, loc.lngLat());
  };
  //trackPlot->onHovered = hoverFn;
  trackSliders->onValueChanged = hoverFn;

  trackPlot->onPanZoom = [=](){
    real start = trackPlot->trackPosToPlotPos(cropStart);
    real end = trackPlot->trackPosToPlotPos(cropEnd);
    trackSliders->setCropHandles(start, end, TrackSliders::NO_UPDATE);
  };

  // Tracks panel
  tracksContent = createColumn();

  Widget* newTrackContent = createColumn();
  newTrackContent->node->setAttribute("box-anchor", "hfill");
  TextEdit* newTrackTitle = createTextEdit();
  TextEdit* newTrackFile = createTextEdit();

  Button* createTrackBtn = createPushbutton("Create");
  Button* cancelTrackBtn = createPushbutton("Cancel");
  createTrackBtn->onClicked = [=](){
    std::string filename = newTrackFile->text();
    if(filename.empty())
      filename = FSPath(MapsApp::baseDir, newTrackTitle->text() + ".gpx").c_str();
    const char* query = "INSERT INTO tracks (title,filename) VALUES (?,?);";
    DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, newTrackTitle->text().c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_TRANSIENT);
    });
    tracks.emplace_back(newTrackTitle->text(), "", filename);
    tracks.back().loaded = true;
    populateTracks(false);
    populateWaypoints(&tracks.back());
    newTrackContent->setVisible(false);
  };
  cancelTrackBtn->onClicked = [=](){ newTrackContent->setVisible(false); };
  newTrackTitle->onChanged = [=](const char* s){ createTrackBtn->setEnabled(s[0]); };
  newTrackContent->addWidget(createTitledRow("Title", newTrackTitle));
  newTrackContent->addWidget(createTitledRow("File", newTrackFile));
  newTrackContent->addWidget(createTitledRow(NULL, createTrackBtn, cancelTrackBtn));
  newTrackContent->setVisible(false);
  tracksContent->addWidget(newTrackContent);

  Button* drawTrackBtn = createToolbutton(MapsApp::uiIcon("draw-track"), "Create Route");
  drawTrackBtn->onClicked = [=](){
    char timestr[64];
    time_t t = mSecSinceEpoch()/1000;
    strftime(timestr, sizeof(timestr), "%FT%H.%M.%S", localtime(&t));  //"%Y-%m-%d %HH%M"
    newTrackTitle->setText(timestr);
    newTrackContent->setVisible(true);
    //drawTrack = !drawTrack;
    //drawTrackBtn->setTitle(drawTrack ? "Finish Track" : "Draw Track");
  };

  Button* loadTrackBtn = createToolbutton(MapsApp::uiIcon("open-folder"), "Load Track");
  loadTrackBtn->onClicked = [=](){
    nfdchar_t* outPath;
    nfdfilteritem_t filterItem[1] = { { "GPX files", "gpx" } };
    nfdresult_t result = NFD_OpenDialog(&outPath, filterItem, 1, NULL);
    if(result != NFD_OKAY)
      return;
    tracks.emplace_back("", "", outPath);
    loadGPX(&tracks.back());
    if(tracks.back().waypoints.empty() && !tracks.back().activeWay()) {
      PLATFORM_LOG("Error loading track!");
      tracks.pop_back();
      return;
    }
    const char* query = "INSERT INTO tracks (title,filename) VALUES (?,?);";
    DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, tracks.back().title.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, tracks.back().filename.c_str(), -1, SQLITE_TRANSIENT);
    });
    populateTracks(false);
    populateStats(&tracks.back());
  };

  Button* recordTrackBtn = createToolbutton(MapsApp::uiIcon("record"), "Record Track");
  recordTrackBtn->onClicked = [=](){
    if(!recordedTrack.tracks.empty())
      populateStats(&recordedTrack);  // show stats panel for recordedTrack, incl pause and stop buttons
    else {
      recordTrack = true;
      char timestr[64];
      time_t t = mSecSinceEpoch()/1000;
      strftime(timestr, sizeof(timestr), "%FT%H.%M.%S", localtime(&t));  //"%Y-%m-%d %HH%M"
      FSPath gpxPath(app->baseDir, std::string(timestr) + ".gpx");
      recordedTrack = GpxFile(timestr, "", gpxPath.path);  //Track{timestr, "", gpxPath.c_str(), "", 0, {}, -1, true, false};
      recordedTrack.loaded = true;
      recordedTrack.tracks.emplace_back();
      recordedTrack.tracks.back().pts.push_back(app->currLocation);
      recordedTrack.marker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(recordedTrack.marker, "layers.recording-track.draw.track");
      populateTracks(false);
      populateStats(&recordedTrack);
      saveTitle->setText(recordedTrack.title.c_str());
      savePath->setText(recordedTrack.filename.c_str());
      saveTrackContent->setVisible(true);
    }
  };

  auto tracksTb = app->createPanelHeader(MapsApp::uiIcon("track"), "Tracks");
  tracksTb->addWidget(drawTrackBtn);
  tracksTb->addWidget(loadTrackBtn);
  tracksTb->addWidget(recordTrackBtn);
  tracksPanel = app->createMapPanel(tracksTb, tracksContent);

  tracksPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      if(tracksDirty)
        populateTracks(false);
    }
    return false;
  });

  archivedContent = createColumn();
  auto archivedHeader = app->createPanelHeader(MapsApp::uiIcon("archive"), "Archived Tracks");
  archivedPanel = app->createMapPanel(archivedHeader, archivedContent);

  // waypoint panel
  wayptContent = new DragDropList;

  Button* mapCenterWayptBtn = createToolbutton(MapsApp::uiIcon("add-pin"), "Add waypoint");
  mapCenterWayptBtn->onClicked = [=](){
    std::string title = fstring("Waypoint %d", activeTrack->wayPtSerial+1);
    activeTrack->addWaypoint({app->getMapCenter(), title});
    activeTrack->modified = true;
    addWaypointItem(activeTrack->waypoints.back());
    if(activeTrack->waypoints.back().routed)
      createRoute(activeTrack);
  };

  bool hasPlugins = !app->pluginManager->routeFns.empty();
  Button* routePluginBtn = createToolbutton(MapsApp::uiIcon(hasPlugins ? "plugin" : "no-plugin"), "Plugin");
  routePluginBtn->setEnabled(hasPlugins);
  if(hasPlugins) {
    Menu* routePluginMenu = createMenu(Menu::VERT_LEFT, false);
    int ii = 0;
    for(auto& fn : app->pluginManager->routeFns) {
      routePluginMenu->addItem(fn.title.c_str(), [=](){ pluginFn = ii; });
      ++ii;
    }
    routePluginBtn->setMenu(routePluginMenu);
  }

  routeModeBtn = createToolbutton(MapsApp::uiIcon("segment"), "Routing");
  Menu* routeModeMenu = createMenu(Menu::VERT_LEFT);
  routeModeMenu->addItem("Direct", MapsApp::uiIcon("segment"), [=](){ setRouteMode("direct"); });
  routeModeMenu->addItem("Walk", MapsApp::uiIcon("walk"), [=](){ setRouteMode("walk"); });
  routeModeMenu->addItem("Cycle", MapsApp::uiIcon("bike"), [=](){ setRouteMode("bike"); });
  routeModeMenu->addItem("Drive", MapsApp::uiIcon("car"), [=](){ setRouteMode("drive"); });
  routeModeBtn->setMenu(routeModeMenu);
  routeModeBtn->setEnabled(hasPlugins);

  Button* wayptsOverflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More options");
  Menu* wayptsOverflow = createMenu(Menu::VERT_LEFT);
  wayptsOverflowBtn->setMenu(wayptsOverflow);

  Button* showAllWptsBtn = createCheckBoxMenuItem("Show all waypoints");
  showAllWptsBtn->onClicked = [=](){
    showAllWaypts = !showAllWaypts;
    showAllWptsBtn->setChecked(showAllWaypts);
    populateWaypoints(activeTrack);
  };
  wayptsOverflow->addItem(showAllWptsBtn);

  auto wayptsTb = app->createPanelHeader(MapsApp::uiIcon("track"), "Waypoints");
  wayptsTb->addWidget(mapCenterWayptBtn);
  wayptsTb->addWidget(routeModeBtn);
  wayptsTb->addWidget(routePluginBtn);
  wayptsTb->addWidget(wayptsOverflowBtn);
  wayptPanel = app->createMapPanel(wayptsTb, NULL, wayptContent);
  wayptPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      if(waypointsDirty)
        populateWaypoints(activeTrack);
    }
    else if(event->type == MapsApp::PANEL_CLOSED) {
      app->pluginManager->cancelRequests(PluginManager::ROUTE);
      directRoutePreview = false;
      app->map->markerSetVisible(previewMarker, false);
      app->crossHair->setVisible(false);
      if(activeTrack->modified)
        activeTrack->modified = !saveGPX(activeTrack);
      //if(editTrackBtn->isChecked())
      //  editTrackBtn->onClicked();
      //app->map->markerSetVisible(trackHoverMarker, false);
      if(activeTrack != &recordedTrack)
        app->map->markerSetStylingFromPath(activeTrack->marker, "layers.track.draw.marker");
      if(!activeTrack->visible)
        showTrack(activeTrack, false);
      activeTrack = NULL;
    }
    return false;
  });

  auto statsTb = app->createPanelHeader(MapsApp::uiIcon("graph-line"), "");
  statsTb->addWidget(pauseRecordBtn);
  statsTb->addWidget(stopRecordBtn);
  statsTb->addWidget(editTrackBtn);
  statsPanel = app->createMapPanel(statsTb, statsContent);
  statsPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == MapsApp::PANEL_CLOSED) {
      if(editTrackBtn->isChecked())
        editTrackBtn->onClicked();
      app->map->markerSetVisible(trackHoverMarker, false);
      if(activeTrack != &recordedTrack)
        app->map->markerSetStylingFromPath(activeTrack->marker, "layers.track.draw.marker");
      if(!activeTrack->visible)
        showTrack(activeTrack, false);
      activeTrack = NULL;
    }
    return false;
  });

  // load tracks for quick menu and for visible tracks
  loadTracks(false);
  YAML::Node vistracks;
  Tangram::YamlPath("+tracks.visible").get(app->config, vistracks);  //node = app->getConfigPath("+places.visible");
  for(auto& node : vistracks) {
    int rowid = node.as<int>(-1);
    for(GpxFile& track : tracks) {
      if(track.rowid == rowid) {
        track.visible = true;
        showTrack(&track, true);
        break;
      }
    }
  }

  // main toolbar button ... quick menu - recent tracks?
  Menu* tracksMenu = createMenu(Menu::VERT_LEFT);
  tracksMenu->addHandler([=](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(tracksMenu->selectFirst(".child-container"));
      if(recordTrack) {
        Button* item = createCheckBoxMenuItem("Current track");
        item->onClicked = [this](){ setTrackVisible(&recordedTrack, !recordedTrack.visible); };
        item->setChecked(recordedTrack.visible);
        tracksMenu->addItem(item);
      }
      for(size_t ii = 0; ii < 9 && ii < tracks.size(); ++ii) {
        Button* item = createCheckBoxMenuItem(tracks[ii].title.c_str());
        item->onClicked = [ii, this](){ setTrackVisible(&tracks[ii], !tracks[ii].visible); };
        item->setChecked(tracks[ii].visible);
        tracksMenu->addItem(item);
      }
    }
    return false;
  });

  Button* tracksBtn = app->createPanelButton(MapsApp::uiIcon("track"), "Tracks", tracksPanel);
  tracksBtn->setMenu(tracksMenu);
  return tracksBtn;
}
