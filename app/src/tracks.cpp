#include "tracks.h"
#include "mapsapp.h"
#include "util.h"
#include "mapwidgets.h"

#include <ctime>
#include <iomanip>
#include "pugixml.hpp"
#include "yaml-cpp/yaml.h"
#include "sqlite3/sqlite3.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "usvg/svgpainter.h"
#include "usvg/svgparser.h"  // for parseColor
#include "usvg/svgwriter.h"  // for serializeColor

using Track = MapsTracks::Track;
using TrackLoc = MapsTracks::TrackLoc;

class TrackPlot : public Widget
{
public:
  TrackPlot();
  void draw(SvgPainter* svgp) const override;
  Rect bounds(SvgPainter* svgp) const override;
  void setTrack(MapsTracks::Track* track);
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

void TrackPlot::setTrack(MapsTracks::Track* track)
{
  minAlt = FLT_MAX;
  maxAlt = -FLT_MAX;
  minSpd = FLT_MAX;
  maxSpd = -FLT_MAX;
  minTime = track->locs.front().time;
  maxTime = track->locs.back().time;
  maxDist = track->locs.back().dist;
  altDistPlot.clear();
  altTimePlot.clear();
  spdDistPlot.clear();
  spdTimePlot.clear();
  altDistPlot.addPoint(track->locs.front().dist, -1000);
  altTimePlot.addPoint(track->locs.front().time, -1000);
  for(auto& tpt : track->locs) {
    altDistPlot.addPoint(Point(tpt.dist, MapsApp::metricUnits ? tpt.alt : tpt.alt*3.28084));
    altTimePlot.addPoint(Point(tpt.time, MapsApp::metricUnits ? tpt.alt : tpt.alt*3.28084));
    minAlt = std::min(minAlt, tpt.alt);
    maxAlt = std::max(maxAlt, tpt.alt);
    spdDistPlot.addPoint(Point(tpt.dist, MapsApp::metricUnits ? tpt.spd*3600*0.001 : tpt.spd*3600*0.000621371));
    spdTimePlot.addPoint(Point(tpt.time, MapsApp::metricUnits ? tpt.spd*3600*0.001 : tpt.spd*3600*0.000621371));
    minSpd = std::min(minSpd, tpt.spd);
    maxSpd = std::max(maxSpd, tpt.spd);
  }
  altDistPlot.addPoint(track->locs.back().dist, -1000);
  altTimePlot.addPoint(track->locs.back().time, -1000);
  if(maxTime - minTime <= 0)
    plotVsDist = true;

  real elev = maxAlt - minAlt;
  minAlt -= 0.05*elev;
  maxAlt += 0.05*elev;
  maxZoom = track->locs.size()/8;  // min 8 points in view
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
Track MapsTracks::loadGPX(const char* gpxfile)
{
  double dist = 0;
  pugi::xml_document doc;
  doc.load_file(gpxfile);
  pugi::xml_node trk = doc.child("gpx").child("trk");
  if(!trk) logMsg("Error loading %s\n", gpxfile);
  Track track = {trk.child("name").child_value(), "", gpxfile, "", 0, {}, -1, false, false};
  //activeTrack.clear();
  //gpxFile = gpxfile;
  while(trk) {
    pugi::xml_node trkseg = trk.child("trkseg");
    while(trkseg) {
      //std::vector<LngLat> track;
      pugi::xml_node trkpt = trkseg.child("trkpt");
      while(trkpt) {
        double lat = trkpt.attribute("lat").as_double();
        double lng = trkpt.attribute("lon").as_double();
        //track.emplace_back(lng, lat);
        pugi::xml_node elenode = trkpt.child("ele");
        double ele = atof(elenode.child_value());
        dist += track.locs.empty() ? 0 : 1000*lngLatDist(LngLat(lng, lat), track.locs.back().lngLat());
        //activeTrack.push_back({track.back(), dist, atof(ele.child_value())});
        double time = 0;
        pugi::xml_node timenode = trkpt.child("time");
        if(timenode) {
          std::tm tmb;
          std::stringstream(timenode.child_value()) >> std::get_time(&tmb, "%Y-%m-%dT%TZ");  //2023-03-31T20:19:15Z
          time = mktime(&tmb);
        }
        float spd = 0;
        if(time > 0 && !track.locs.empty())
          spd = (dist - track.locs.back().dist)/(time - track.locs.back().time);
        track.locs.push_back({time, lat, lng, 0, ele, 0, /*dir*/0, 0, spd, 0, dist});
        trkpt = trkpt.next_sibling("trkpt");
      }
      trkseg = trkseg.next_sibling("trkseg");
    }
    trk = trk.next_sibling("trk");
  }
  return track;
}

void MapsTracks::tapEvent(LngLat location)
{
  if(!drawTrack)
    return;
  auto& locs = drawnTrack.locs;
  double dist = locs.empty() ? 0 : locs.back().dist + 1000*lngLatDist(locs.back().lngLat(), location);
  double time = 0;
  locs.push_back({time, location.latitude, location.longitude, 0, /*ele*/0, 0, /*dir*/0, 0, /*spd*/0, 0, dist});
  showTrack(&drawnTrack);  //, "layers.track.draw.selected-track");
}

void MapsTracks::updateLocation(const Location& loc)
{
  if(!recordTrack)
    return;
  if(recordedTrack.locs.empty()) {
    recordedTrack.locs.push_back(loc);
    recordedTrack.locs.back().dist = 0;
  }
  else {
    auto& prev = recordedTrack.locs.back();
    // since altitude is less precise than pos, I don't think we should do sqrt(dist^2 + vert^2)
    double dist = 1000*lngLatDist(loc.lngLat(), prev.lngLat());
    double vert = loc.alt - prev.alt;
    double dt = loc.time - prev.time;
    if(dist > minTrackDist || dt > minTrackTime || vert > minTrackDist) {
      double d0 = recordedTrack.locs.back().dist;
      recordedTrack.locs.push_back(loc);
      recordedTrack.locs.back().dist = d0 + dist;
      if(loc.spd == 0)
        recordedTrack.locs.back().spd = dist/dt;
      if(recordedTrack.visible)
        showTrack(&recordedTrack);  // rebuild marker
      if(activeTrack == &recordedTrack)
        populateStats(&recordedTrack);
      if(loc.time > recordLastSave + 60) {
        saveGPX(&recordedTrack);
        recordLastSave = loc.time;
      }
    }
  }
}

void MapsTracks::showTrack(Track* track)  //, const char* styling)
{
  if(track->locs.empty() && !track->gpxFile.empty())
    track->locs = std::move(loadGPX(track->gpxFile.c_str()).locs);
  std::vector<LngLat> pts;
  for(const TrackLoc& loc : track->locs)
    pts.push_back(loc.lngLat());
  // choose unused color automatically, but allow user to change color (line width, dash too?)
  if(track->marker <= 0) {
    track->marker = app->map->markerAdd();
    app->map->markerSetStylingFromPath(track->marker, "layers.track.draw.track");  //styling);
  }
  app->map->markerSetPolyline(track->marker, pts.data(), pts.size());
  if(!track->style.empty())
    app->map->markerSetProperties(track->marker, {{{"color", track->style}}});
}

bool MapsTracks::saveGPX(Track* track)
{
  // saving track
  char timebuf[256];
  pugi::xml_document doc;
  pugi::xml_node trk = doc.append_child("gpx").append_child("trk");
  trk.append_child("name").append_child(pugi::node_pcdata).set_value(track->title.c_str());
  pugi::xml_node seg = trk.append_child("trkseg");
  for(const TrackLoc& loc : track->locs) {
    pugi::xml_node trkpt = seg.append_child("trkpt");
    trkpt.append_attribute("lat").set_value(fstring("%.7f", loc.lat).c_str());
    trkpt.append_attribute("lon").set_value(fstring("%.7f", loc.lng).c_str());
    trkpt.append_child("ele").append_child(pugi::node_pcdata).set_value(fstring("%.1f", loc.alt).c_str());
    if(loc.time > 0) {
      time_t t = time_t(loc.time);
      strftime(timebuf, sizeof(timebuf), "%FT%TZ", gmtime(&t));
      trkpt.append_child("time").append_child(pugi::node_pcdata).set_value(timebuf);
    }
    // should we save direction or speed? position, altitude error?
  }
  return doc.save_file(track->gpxFile.c_str(), "  ");
}

Widget* MapsTracks::createTrackEntry(Track* track)
{
  Button* item = new Button(trackListProto->clone());
  item->onClicked = [=](){ populateStats(track); };
  Widget* container = item->selectFirst(".child-container");

  Button* showBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_pin.svg"), "Show");
  showBtn->onClicked = [=](){
    track->visible = !showBtn->checked();
    showBtn->setChecked(track->visible);
    //std::string q1 = fstring("UPDATE lists_state SET visible = %d WHERE list_id = ?;", visible ? 1 : 0);
    //DB_exec(app->bkmkDB, q1.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
    // assume recordedTrack is dirty (should we introduce a Track.dirty flag?)
    if(!track->visible || (track->marker > 0 && track != &recordedTrack))
      app->map->markerSetVisible(track->marker, track->visible);
    else
      showTrack(track);  //, "layers.track.draw.track");
  };
  showBtn->setChecked(track->visible);
  container->addWidget(showBtn);

  auto onColor = [this, track](Color color){
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
  Button* colorBtn = createColorPicker(markerColors, parseColor(track->style, Color::BLUE), onColor);
  container->addWidget(colorBtn);

  if(track->rowid >= 0) {
    Button* overflowBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_overflow.svg"), "More");
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
          tracks.erase(it);
          break;
        }
      }
      app->gui->deleteWidget(item);  //populateTracks(archived);  // refresh
    });
    overflowBtn->setMenu(overflowMenu);
    container->addWidget(overflowBtn);
  }

  // track detail: date? duration? distance?
  item->selectFirst(".title-text")->setText(track->title.c_str());
  item->selectFirst(".detail-text")->setText(track->detail.c_str());
  return item;
}

void MapsTracks::populateTracks(bool archived)
{
  tracksDirty = false;
  app->showPanel(archived ? archivedPanel : tracksPanel, archived);
  Widget* content = archived ? archivedContent : tracksContent;
  app->gui->deleteContents(content, ".listitem");

  const char* query = "SELECT rowid, title, filename, strftime('%Y-%m-%d', timestamp, 'unixepoch'), style FROM tracks WHERE archived = ?;";
  DB_exec(app->bkmkDB, query, [this, archived](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    const char* title = (const char*)(sqlite3_column_text(stmt, 1));
    const char* filename = (const char*)(sqlite3_column_text(stmt, 2));
    const char* date = (const char*)(sqlite3_column_text(stmt, 3));
    const char* style = (const char*)(sqlite3_column_text(stmt, 4));
    for(Track& track : tracks) {
      if(track.rowid == rowid)
        return;
    }
    tracks.push_back({title, date, filename, style ? style : "", 0, {}, rowid, false, archived});
  }, [=](sqlite3_stmt* stmt){
    sqlite3_bind_int(stmt, 1, archived ? 1 : 0);
  });

  if(recordTrack && !archived)
    content->addWidget(createTrackEntry(&recordedTrack));
  for(Track& track : tracks) {
    if(track.archived == archived)
      content->addWidget(createTrackEntry(&track));
  }
  if(!archived) {
    Button* item = new Button(trackListProto->clone());
    item->onClicked = [this](){ populateTracks(true); };
    item->selectFirst(".title-text")->setText("Archived Tracks");
    content->addWidget(item);
  }
}

void MapsTracks::populateStats(Track* track)
{
  app->showPanel(statsPanel, true);
  statsPanel->selectFirst(".panel-title")->setText(track->title.c_str());

  if(track->marker <= 0)
    showTrack(track);  //, "layers.track.draw.selected-track");
  else if(!track->visible)
    app->map->markerSetVisible(track->marker, true);

  bool isRecTrack = track == &recordedTrack;
  pauseRecordBtn->setVisible(isRecTrack);
  stopRecordBtn->setVisible(isRecTrack);
  if(!isRecTrack)
    app->map->markerSetStylingFromPath(track->marker, "layers.selected-track.draw.track");

  if(!origLocs.empty())
    track->locs.front().dist = 0;
  // how to calculate max speed?
  double trackDist = 0, trackAscent = 0, trackDescent = 0, ascentTime = 0, descentTime = 0, movingTime = 0;
  double currSpeed = 0, maxSpeed = 0;
  LngLat minLngLat(track->locs.front().lngLat()), maxLngLat(track->locs.front().lngLat());
  for(size_t ii = 1; ii < track->locs.size(); ++ii) {
    const Location& prev = track->locs[ii-1];
    Location& loc = track->locs[ii];
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
      loc.dist = trackDist;
  }

  if(activeTrack != track) {
    if(!app->map->lngLatToScreenPosition(minLngLat.longitude, minLngLat.latitude)
        || !app->map->lngLatToScreenPosition(maxLngLat.longitude, maxLngLat.latitude)) {
      app->map->setCameraPositionEased(app->map->getEnclosingCameraPosition(minLngLat, maxLngLat, {32}), 0.5f);
    }
    activeTrack = track;
  }
  trackPlot->setTrack(track);

  const Location& loc = track->locs.back();
  //std::string posStr = fstring("%.6f, %.6f", loc.lat, loc.lng);
  //statsContent->selectFirst(".track-position")->setText(posStr.c_str());
  statsContent->selectFirst(".track-latitude")->setText(fstring("%.6f", loc.lat).c_str());
  statsContent->selectFirst(".track-longitude")->setText(fstring("%.6f", loc.lng).c_str());

  std::string altStr = app->metricUnits ? fstring("%.0f m", loc.alt) : fstring("%.0f ft", loc.alt*3.28084);
  statsContent->selectFirst(".track-altitude")->setText(altStr.c_str());

  // m/s -> kph or mph
  std::string speedStr = app->metricUnits ? fstring("%.2f km/h", currSpeed*3.6) : fstring("%.2f mph", currSpeed*2.23694);
  statsContent->selectFirst(".track-speed")->setText(speedStr.c_str());

  double ttot = track->locs.back().time - track->locs.front().time;
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

static TrackLoc interpLoc(const TrackLoc& a, const TrackLoc& b, double f)
{
  float g = float(f);
  return TrackLoc{
      f*b.time + (1-f)*a.time,
      f*b.lat + (1-f)*a.lat,
      f*b.lng + (1-f)*a.lng,
      g*b.poserr + (1-g)*a.poserr,
      f*b.alt + (1-f)*a.alt,
      g*b.alterr + (1-g)*a.alterr,
      g*b.dir + (1-g)*a.dir,
      g*b.direrr + (1-g)*a.direrr,
      g*b.spd + (1-g)*a.spd,
      g*b.spderr + (1-g)*a.spderr,
      f*b.dist + (1-f)*a.dist};
}

static TrackLoc interpTrackDist(const std::vector<TrackLoc>& locs, double s, size_t* idxout)
{
  double sd = s*locs.back().dist;
  size_t jj = 0;
  while(locs[jj].dist < sd) ++jj;
  if(idxout) *idxout = jj;
  double f = (sd - locs[jj-1].dist)/(locs[jj].dist - locs[jj-1].dist);
  return interpLoc(locs[jj-1], locs[jj], f);
}

static TrackLoc interpTrackTime(const std::vector<TrackLoc>& locs, double s, size_t* idxout)
{
  double st = s*locs.back().time + (1-s)*locs.front().time;
  size_t jj = 0;
  while(locs[jj].time < st) ++jj;
  if(idxout) *idxout = jj;
  double f = (st - locs[jj-1].time)/(locs[jj].time - locs[jj-1].time);
  return interpLoc(locs[jj-1], locs[jj], f);
}

TrackLoc MapsTracks::interpTrack(const std::vector<TrackLoc>& locs, double s, size_t* idxout)
{
  s = std::min(std::max(s, 0.0), 1.0);
  return trackPlot->plotVsDist ? interpTrackDist(locs, s, idxout) : interpTrackTime(locs, s, idxout);
}

Widget* MapsTracks::createPanel()
{
  static const char* trackListProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g class="child-container" layout="flex" flex-direction="row" box-anchor="hfill">
        <g class="image-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_drawer.svg"/>
        </g>
        <g layout="box" box-anchor="vfill">
          <text class="title-text" box-anchor="left" margin="0 10"></text>
          <text class="detail-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
        </g>

        <rect class="stretch" fill="none" box-anchor="fill" width="20" height="20"/>

      </g>
    </g>
  )";
  trackListProto.reset(loadSVGFragment(trackListProtoSVG));

  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS tracks(title TEXT, filename TEXT, style TEXT,"
      " timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)), archived INTEGER DEFAULT 0);");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS tracks_state(track_id INTEGER, ordering INTEGER, visible INTEGER);");

  // colors for tracks; currently shared with bookmarks - if the remains the case, we should dedup
  for(const auto& colorstr : app->config["colors"])
    markerColors.push_back(parseColor(colorstr.Scalar()));

  // Stats panel
  statsContent = createColumn();

  Widget* saveTrackContent = createColumn();
  TextEdit* saveTitle = createTextEdit();
  TextEdit* savePath = createTextEdit();
  CheckBox* replaceTrackCb = createCheckBox("Replace track", false);
  Button* saveTrackBtn = createPushbutton("Apply");
  Button* restoreTrackBtn = createPushbutton("Cancel");

  savePath->onChanged = [=](const char* path){
    bool newfile = path != activeTrack->gpxFile;
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
    TrackLoc startloc = interpTrack(activeTrack->locs, cropStart);
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
    TrackLoc endloc = interpTrack(activeTrack->locs, cropEnd);
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
    TrackLoc loc = interpTrack(activeTrack->locs, trackSliders->value());
    // TODO: how to open with add bookmark section expanded?  pass flag forwarded to getPlaceInfoSubSection()?
    app->setPickResult(loc.lngLat(), activeTrack->title + " waypoint", "");
  };

  Button* cropTrackBtn = createToolbutton(NULL, "Crop to segment", true);
  cropTrackBtn->onClicked = [=](){
    if(origLocs.empty()) origLocs = activeTrack->locs;
    const std::vector<TrackLoc>& locs = activeTrack->locs;
    std::vector<TrackLoc> newlocs;
    size_t startidx, endidx;
    newlocs.push_back(interpTrack(locs, cropStart, &startidx));
    auto endloc = interpTrack(locs, cropEnd, &endidx);
    newlocs.insert(newlocs.end(), locs.begin() + startidx, locs.begin() + endidx);
    newlocs.push_back(endloc);
    activeTrack->locs.swap(newlocs);
    trackSliders->setCropHandles(0, 1, TrackSliders::FORCE_UPDATE);
    showTrack(activeTrack);  // rebuild marker
    populateStats(activeTrack);
  };

  Button* appendTrackBtn = createToolbutton(NULL, "Append track", true);
  appendTrackBtn->onClicked = [this](){
    if(!selectTrackDialog) {
      selectTrackDialog.reset(createSelectDialog("Choose Track", SvgGui::useFile(":/icons/ic_menu_select_path.svg")));
      selectTrackDialog->onSelected = [this](int idx){
        if(origLocs.empty()) origLocs = activeTrack->locs;
        activeTrack->locs.insert(activeTrack->locs.end(), tracks[idx].locs.begin(), tracks[idx].locs.end());
        showTrack(activeTrack);  // rebuild marker
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

  Button* moreTrackOptionsBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_overflow.svg"), "More options");
  Menu* trackPlotOverflow = createMenu(Menu::VERT_LEFT);
  moreTrackOptionsBtn->setMenu(trackPlotOverflow);

  trackPlotOverflow->addItem("Reverse Track", [this](){
    if(origLocs.empty()) origLocs = activeTrack->locs;
    std::reverse(activeTrack->locs.begin(), activeTrack->locs.end());
    showTrack(activeTrack);  // rebuild marker
    populateStats(activeTrack);
  });

  trackPlotOverflow->addItem("Delete Segment", [=](){
    if(origLocs.empty()) origLocs = activeTrack->locs;
    const std::vector<TrackLoc>& locs = activeTrack->locs;
    std::vector<TrackLoc> newlocs;
    size_t startidx, endidx;
    auto startloc = interpTrack(locs, cropStart, &startidx);
    auto endloc = interpTrack(locs, cropEnd, &endidx);
    newlocs.insert(newlocs.end(), locs.begin(), locs.begin() + startidx);
    newlocs.push_back(startloc);
    newlocs.push_back(endloc);
    newlocs.insert(newlocs.end(), locs.begin() + endidx, locs.end());
    activeTrack->locs.swap(newlocs);
    trackSliders->setCropHandles(0, 1, TrackSliders::FORCE_UPDATE);
    showTrack(activeTrack);  // rebuild marker
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

  Button* editTrackBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_draw.svg"), "Edit Track");
  pauseRecordBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_select_ruled.svg"), "Pause");
  stopRecordBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_select.svg"), "Stop");

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
        activeTrack->locs = std::move(origLocs);
        showTrack(activeTrack);  // rebuild marker
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
      sqlite3_bind_text(stmt, 2, recordedTrack.gpxFile.c_str(), -1, SQLITE_TRANSIENT);
    });
    recordedTrack.rowid = sqlite3_last_insert_rowid(app->bkmkDB);
    tracks.push_back(std::move(recordedTrack));
    recordedTrack = Track{};
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
      savePath->setText(activeTrack->gpxFile.c_str());
      saveTrackBtn->setEnabled(false);
    }
  };
  restoreTrackBtn->onClicked = [=](){ editTrackBtn->onClicked(); };

  saveTrackBtn->onClicked = [=](){
    std::string prevFile = activeTrack->gpxFile;
    activeTrack->title = saveTitle->text();
    activeTrack->gpxFile = savePath->text();
    if(saveGPX(activeTrack)) {
      bool replace = replaceTrackCb->isChecked();
      if(activeTrack->rowid >= 0) {
        const char* query = replace ? "UPDATE tracks SET title = ?, filename = ? WHERE rowid = ?;"
            : "INSERT INTO tracks (title,filename) VALUES (?,?);";
        DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
          sqlite3_bind_text(stmt, 1, activeTrack->title.c_str(), -1, SQLITE_TRANSIENT);
          sqlite3_bind_text(stmt, 2, activeTrack->gpxFile.c_str(), -1, SQLITE_TRANSIENT);
          if(replace)
            sqlite3_bind_int(stmt, 3, activeTrack->rowid);
        });
        if(!replace)
          activeTrack->rowid = sqlite3_last_insert_rowid(app->bkmkDB);
      }
      if(replace && !prevFile.empty() && prevFile != activeTrack->gpxFile)
        removeFile(prevFile);
      origLocs.clear();
      editTrackBtn->onClicked();  // close edit track view
    }
    else
      activeTrack->gpxFile = prevFile;
    tracksDirty = true;
  };

  auto hoverFn = [this](real s){
    if(s < 0 || s > 1 || !activeTrack) {
      app->map->markerSetVisible(trackHoverMarker, false);
      return;
    }
    TrackLoc loc = interpTrack(activeTrack->locs, s);
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

  Button* drawTrackBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_draw.svg"), "Draw Track");
  drawTrackBtn->onClicked = [=](){
    drawTrack = !drawTrack;
    drawTrackBtn->setTitle(drawTrack ? "Finish Track" : "Draw Track");
  };

  Widget* loadTrackPanel = createColumn();
  Button* loadTrackBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_folder.svg"), "Load Track");
  loadTrackBtn->onClicked = [=](){
    loadTrackPanel->setVisible(true);
  };

  Button* recordTrackBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_save.svg"), "Record Track");
  recordTrackBtn->onClicked = [=](){
    if(!recordedTrack.locs.empty())
      populateStats(&recordedTrack);  // show stats panel for recordedTrack, incl pause and stop buttons
    else {
      recordTrack = true;
      char timestr[64];
      time_t t = mSecSinceEpoch()/1000;
      strftime(timestr, sizeof(timestr), "%FT%H.%M.%S", localtime(&t));  //"%Y-%m-%d %HH%M"
      FSPath gpxPath(app->baseDir, std::string(timestr) + ".gpx");
      recordedTrack = Track{timestr, "", gpxPath.c_str(), "", 0, {}, -1, true, false};
      recordedTrack.locs.push_back(app->currLocation);
      recordedTrack.marker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(recordedTrack.marker, "layers.recording-track.draw.track");
      populateTracks(false);
      populateStats(&recordedTrack);
      saveTitle->setText(recordedTrack.title.c_str());
      savePath->setText(recordedTrack.gpxFile.c_str());
      saveTrackContent->setVisible(true);
    }
  };

  TextEdit* gpxPath = createTextEdit();
  Button* addGpxBtn = createPushbutton("Add");

  addGpxBtn->onClicked = [=](){
    auto track = loadGPX(gpxPath->text().c_str());
    if(track.locs.empty()) {
      PLATFORM_LOG("Error loading track!");
      return;
    }
    const char* query = "INSERT INTO tracks (title,filename) VALUES (?,?);";
    DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, track.title.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, track.gpxFile.c_str(), -1, SQLITE_TRANSIENT);
    });
    tracks.push_back(std::move(track));
    populateTracks(false);
    populateStats(&tracks.back());
    loadTrackPanel->setVisible(false);
  };

  loadTrackPanel->addWidget(createTitledRow("GPX File", gpxPath));
  loadTrackPanel->addWidget(addGpxBtn);
  loadTrackPanel->setVisible(false);
  tracksContent->addWidget(loadTrackPanel);

  auto tracksTb = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_select_path.svg"), "Tracks");
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
  auto archivedHeader = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Archived Tracks");
  archivedPanel = app->createMapPanel(archivedHeader, archivedContent);

  auto statsTb = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_bookmark.svg"), "");
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
        app->map->markerSetVisible(activeTrack->marker, false);
      activeTrack = NULL;
    }
    return false;
  });

  // main toolbar button ... quick menu - recent tracks?
  /*Menu* sourcesMenu = createMenu(Menu::VERT_LEFT);
  sourcesMenu->autoClose = true;
  sourcesMenu->addHandler([this, sourcesMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(sourcesMenu->selectFirst(".child-container"));

      for(int ii = 0; ii < 10 && ii < sourceKeys.size(); ++ii) {
        sourcesMenu->addItem(mapSources[sourceKeys[ii]]["title"].Scalar().c_str(),
            [ii, this](){ sourceCombo->setIndex(ii); rebuildSource(); });
      }

    }
    return false;
  });*/

  Button* tracksBtn = app->createPanelButton(SvgGui::useFile(":/icons/ic_menu_select_path.svg"), "Tracks");
  //tracksBtn->setMenu(sourcesMenu);
  tracksBtn->onClicked = [=](){
    populateTracks(false);
  };

  return tracksBtn;
}
