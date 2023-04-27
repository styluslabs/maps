#include "tracks.h"
#include "mapsapp.h"
#include "util.h"
#include "mapwidgets.h"

#include <ctime>
#include <iomanip>
#include "pugixml.hpp"
#include "sqlite3/sqlite3.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "usvg/svgpainter.h"


class TrackPlot : public Widget
{
public:
  TrackPlot();
  void draw(SvgPainter* svgp) const override;
  Rect bounds(SvgPainter* svgp) const override;
  void setTrack(MapsTracks::Track* track);

  std::function<void(real x)> onHovered;

  Path2D plot;
  Rect mBounds;
  double minAlt;
  double maxAlt;
  double maxDist;

  real zoomScale = 1;
  real zoomOffset = 0;

  static Color bgColor;

private:
  real prevCOM = 0;
  real prevPinchDist = 0;
};

TrackPlot::TrackPlot() : Widget(new SvgCustomNode), mBounds(Rect::wh(200, 70))
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
        zoomOffset += pinchcenter - prevCOM;
        zoomScale *= pinchdist/prevPinchDist;
        redraw();
      }
      prevCOM = pinchcenter;
      prevPinchDist = pinchdist;
    }
    else if(event->type == SDL_FINGERMOTION && gui->pressedWidget == this) {
      zoomOffset += event->tfinger.x - prevCOM;
      prevCOM = event->tfinger.x;
      redraw();
    }
    else if(event->type == SDL_FINGERMOTION && !gui->pressedWidget)
      onHovered((event->tfinger.x - mBounds.left)/mBounds.width());
    else if(event->type == SvgGui::LEAVE)
      onHovered(-1);
    else
      return false;
    return true;
  });
}

void TrackPlot::setTrack(MapsTracks::Track* track)
{
  minAlt = FLT_MAX;
  maxAlt = 0;
  maxDist = track->locs.back().dist;
  plot.clear();
  for(auto& tpt : track->locs) {
    plot.addPoint(Point(tpt.dist, MapsApp::metricUnits ? tpt.alt : tpt.alt*3.28084));
    minAlt = std::min(minAlt, tpt.alt);
    maxAlt = std::max(maxAlt, tpt.alt);
  }
}

Rect TrackPlot::bounds(SvgPainter* svgp) const
{
  return svgp->p->getTransform().mapRect(mBounds);
}

// we'll assume pen can't be changed when preview is displayed, and thus never call redraw()
void TrackPlot::draw(SvgPainter* svgp) const
{
  Painter* p = svgp->p;
  int w = mBounds.width() - 4;
  int h = mBounds.height() - 4;
  p->translate(2, 2);
  p->clipRect(Rect::wh(w, h));
  // use color of current page as background
  p->fillRect(Rect::wh(w, h), bgColor);  //doc->getCurrPageColor());

  // labels
  p->setFillBrush(Color::BLACK);
  real labelw = 0;
  int nvert = 5;
  double dh = (maxAlt - minAlt)/nvert;
  for(int ii = 0; ii < nvert; ++ii)
    labelw = std::max(labelw, p->drawText(0, ii*h/nvert, fstring("%.0f", minAlt + ii*dh).c_str()));
  int nhorz = 5;
  for(int ii = 0; ii < nhorz; ++ii)
    p->drawText(ii*w/nhorz, 0, fstring("%.0f", ii*maxDist).c_str());

  // TODO: need to flip y direction!
  // axes
  //drawCheckerboard(p, w, h, 4, 0x18000000);
  p->setStroke(Color::BLUE, 1.5);
  p->setFillBrush(Brush::NONE);
  p->drawLine(Point(labelw + 5, 15), Point(labelw + 5, h));
  p->drawLine(Point(labelw + 5, 15), Point(w, 15));

  // markers
  p->setStroke(Color::GREEN, 1.5);
  for(real x : markers)
    p->drawLine(Point(x, 15), Point(x, h));

  // plot
  p->clipRect(Rect::ltrb(labelw + 10, 15, w, h));  // clip plot to axes

  p->setStroke(Color::RED, 2.0);
  p->translate(-minAlt, 0);
  p->scale(w/maxDist, h/dh);
  p->translate(labelw + 10, 15);

  p->translate(zoomOffset, 0);
  p->scale(zoomScale, 1);

  p->drawPath(plot);
}


class TrackSliders : public Slider
{
public:
  TrackSliders(SvgNode* n);
  std::function<void()> onCropHandlesChanged;

  real startHandlePos = 0;
  real endHandlePos = 1;

private:
  Widget* startHandle;
  Widget* endHandle;
};

// how would begin + end sliders interact w/ middle slider?  move through it?  push it along? moving begin/end slider hides middle slider?
// ... if middle slider hidden, how to get it back?
// - begin/end slider pushes other along?

TrackSliders::TrackSliders(SvgNode* n) : Slider(n)
{
  startHandle = new Button(containerNode()->selectFirst(".start-handle"));
  endHandle = new Button(containerNode()->selectFirst(".end-handle"));

  startHandle->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == startHandle) {
      Rect rect = sliderBg->node->bounds();
      startHandlePos = (event->tfinger.x - rect.left)/rect.width();
      startHandle->setLayoutTransform(Transform2D().translate(rect.width()*startHandlePos, 0));
      if(startHandlePos > endHandlePos) {
        endHandlePos = startHandlePos;
        endHandle->setLayoutTransform(Transform2D().translate(rect.width()*endHandlePos, 0));
      }
      if(onCropHandlesChanged)
        onCropHandlesChanged();
      return true;
    }
    return false;
  });

  endHandle->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == startHandle) {
      Rect rect = sliderBg->node->bounds();
      endHandlePos = (event->tfinger.x - rect.left)/rect.width();
      endHandle->setLayoutTransform(Transform2D().translate(rect.width()*endHandlePos, 0));
      if(startHandlePos > endHandlePos) {
        startHandlePos = endHandlePos;
        startHandle->setLayoutTransform(Transform2D().translate(rect.width()*startHandlePos, 0));
      }
      if(onCropHandlesChanged)
        onCropHandlesChanged();
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
}

Slider* createSlider()
{
  Slider* slider = new Slider(widgetNode("#slider"));
  slider->isFocusable = true;
  return slider;
}



TrackPlot* createTrackPlot()
{
  static const char* slidersSVG = R"#(
    <g id="slider" class="slider" box-anchor="hfill" layout="box">
      <rect class="slider-bg background" box-anchor="hfill" width="200" height="48"/>
      <g class="left-slider-handle">
        <rect class="slider-handle-outer" x="-6" y="-2" width="12" height="16"/>
        <rect class="slider-handle-inner" x="-4" y="0" width="8" height="12"/>
      </g>

      <g class="right-slider-handle">
        <rect class="slider-handle-outer" x="-6" y="-2" width="12" height="16"/>
        <rect class="slider-handle-inner" x="-4" y="0" width="8" height="12"/>
      </g>

    </g>
  )#";

  sliderHandle = new Button(containerNode()->selectFirst(".slider-handle"));
  // prevent global layout when moving slider handle - note that container bounds change when handle moves
  selectFirst(".slider-handle-container")->setLayoutIsolate(true);

  sliderHandle->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == sliderHandle) {
      Rect rect = sliderBg->node->bounds();
      updateValue((event->tfinger.x - rect.left)/rect.width());  //event->motion.x
      return true;
    }
    return false;
  });

}

// https://www.topografix.com/gpx_manual.asp
MapsTracks::Track MapsTracks::loadGPX(const char* gpxfile)
{
  double dist = 0;
  pugi::xml_document doc;
  doc.load_file(gpxfile);
  pugi::xml_node trk = doc.child("gpx").child("trk");
  if(!trk) logMsg("Error loading %s\n", gpxfile);
  Track track = {trk.child("name").child_value(), "", gpxfile, 0, {}, -1, false, false};
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
        dist += track.locs.empty() ? 0 : lngLatDist(LngLat(lng, lat), track.locs.back().lngLat());
        //activeTrack.push_back({track.back(), dist, atof(ele.child_value())});
        double time = 0;
        pugi::xml_node timenode = trkpt.child("time");
        if(timenode) {
          std::tm tmb;
          std::stringstream(timenode.child_value()) >> std::get_time(&tmb, "%Y-%m-%dT%TZ");  //2023-03-31T20:19:15Z
          time = mktime(&tmb);
        }
        track.locs.push_back({{time, lat, lng, 0, ele, 0, /*dir*/0, 0, /*spd*/0, 0}, dist});
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
  double dist = locs.empty() ? 0 : locs.back().dist + lngLatDist(locs.back().lngLat(), location);
  double time = 0;
  locs.push_back({{time, location.latitude, location.longitude, 0, /*ele*/0, 0, /*dir*/0, 0, /*spd*/0, 0}, dist});
  showTrack(&drawnTrack);  //, "layers.track.draw.selected-track");
}

void MapsTracks::updateLocation(const Location& _loc)
{
  if(!recordTrack)
    return;
  if(recordedTrack.locs.empty())
    recordedTrack.locs.push_back({_loc, 0});
  else {
    auto& prev = recordedTrack.locs.back();
    // since altitude is less precise than pos, I don't think we should do sqrt(dist^2 + vert^2)
    double dist = lngLatDist(_loc.lngLat(), prev.lngLat());
    double vert = _loc.alt - prev.alt;
    double dt = _loc.time - prev.time;
    if(dist > minTrackDist || dt > minTrackTime || vert > minTrackDist) {
      recordedTrack.locs.push_back({_loc, recordedTrack.locs.back().dist + dist});
      if(activeTrack == &recordedTrack)
        populateStats(&recordedTrack);
      else if(recordedTrack.visible)  //if(recordedTrack.marker > 0)
        showTrack(&recordedTrack);  //, "layers.track.draw.recorded-track");
      if(_loc.time > recordLastSave + 60) {
        saveGPX(&recordedTrack);
        recordLastSave = _loc.time;
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
    trkpt.append_attribute("lng").set_value(loc.lng);
    trkpt.append_attribute("lat").set_value(loc.lat);
    trkpt.append_child("ele").append_child(pugi::node_pcdata).set_value(fstring("%.1f", loc.alt).c_str());
    time_t t = time_t(loc.time);
    strftime(timebuf, sizeof(timebuf), "%FT%TZ", gmtime(&t));
    trkpt.append_child("time").append_child(pugi::node_pcdata).set_value(timebuf);
    // should we save direction or speed? position, altitude error?
  }
  return doc.save_file(track->gpxFile.c_str(), "  ");
}

Widget* MapsTracks::createTrackEntry(Track* track)
{
  Button* item = new Button(trackListProto->clone());
  item->onClicked = [&](){ populateStats(track); };
  Button* showBtn = new Button(item->containerNode()->selectFirst(".visibility-btn"));
  showBtn->onClicked = [=](){
    track->visible = !showBtn->checked();
    showBtn->setChecked(track->visible);
    //std::string q1 = fstring("UPDATE lists_state SET visible = %d WHERE list_id = ?;", visible ? 1 : 0);
    //DB_exec(app->bkmkDB, q1.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
    if(!track->visible || track->marker > 0)
      app->map->markerSetVisible(track->marker, track->visible);
    else
      showTrack(track);  //, "layers.track.draw.track");
  };

  if(track->rowid >= 0) {
    Button* overflowBtn = new Button(item->containerNode()->selectFirst(".overflow-btn"));
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowMenu->addItem(track->archived ? "Unarchive" : "Archive", [=](){
      //std::string q1 = fstring("UPDATE lists_state SET order = (SELECT COUNT(1) FROM lists WHERE archived = %d) WHERE list_id = ?;", archived ? 0 : 1);
      //DB_exec(app->bkmkDB, q1.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
      std::string q2 = fstring("UPDATE tracks SET archived = %d WHERE rowid = ?;", track->archived ? 0 : 1);
      DB_exec(app->bkmkDB, q2.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, track->rowid); });
      app->gui->deleteWidget(item);
      track->archived = !track->archived;
      if(!track->archived) tracksDirty = true;
    });

    overflowMenu->addItem("Delete", [=](){
      DB_exec(app->bkmkDB, "DELETE FROM tracks WHERE rowid = ?", NULL, [=](sqlite3_stmt* stmt){
        sqlite3_bind_int(stmt, 1, track->rowid);
      });

      for(auto it = tracks.begin(); it != tracks.end(); ++it) {
        if(it->rowid == track->rowid)
          tracks.erase(it);
      }

      app->gui->deleteWidget(item);  //populateTracks(track.archived);  // refresh
    });
    overflowBtn->setMenu(overflowMenu);
  }

  // track detail: date? duration? distance?
  item->selectFirst(".title-text")->setText(track->title.c_str());
  item->selectFirst(".detail-text")->setText(track->detail.c_str());
  return item;
}

void MapsTracks::populateTracks(bool archived)
{
  app->showPanel(archived ? archivedPanel : tracksPanel);
  Widget* content = archived ? archivedContent : tracksContent;
  app->gui->deleteContents(content, ".listitem");

  const char* query = "SELECT rowid, title, filename, strftime('%Y-%m-%d', timestamp, 'unixepoch') FROM tracks WHERE archived = ?;";
  DB_exec(app->bkmkDB, query, [this, archived](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    std::string title = (const char*)(sqlite3_column_text(stmt, 1));
    std::string filename = (const char*)(sqlite3_column_text(stmt, 2));
    std::string date = (const char*)(sqlite3_column_text(stmt, 3));
    for(auto& track : tracks) {
      if(track.rowid == rowid)
        return;
    }
    tracks.push_back({title, date, filename, 0, {}, rowid, false, archived});
  }, [=](sqlite3_stmt* stmt){
    sqlite3_bind_int(stmt, 1, archived ? 1 : 0);
  });

  if(recordTrack && !archived)
    content->addWidget(createTrackEntry(&recordedTrack));
  for(Track& track : tracks)
    content->addWidget(createTrackEntry(&track));
  if(!archived) {
    Button* item = new Button(trackListProto->clone());
    item->selectFirst(".visibility-btn")->setVisible(false);
    item->onClicked = [this](){ populateTracks(true); };
    item->selectFirst(".title-text")->setText("Archived Tracks");
    content->addWidget(item);
  }
}

void MapsTracks::populateStats(Track* track)
{
  app->showPanel(statsPanel);
  statsPanel->selectFirst(".panel-title")->setText(track->title.c_str());
  trackPlot->setTrack(track);
  activeTrack = track;

  if(track->marker <= 0)
    showTrack(track);  //, "layers.track.draw.selected-track");
  if(track != &recordedTrack)
    app->map->markerSetStylingFromString(track->marker, "layers.track.draw.selected-track");

  // how to calculate max speed?
  double trackDist = 0, trackAscent = 0, trackDescent = 0, ascentTime = 0, descentTime = 0, movingTime = 0;
  double currSpeed = 0, maxSpeed = 0;
  Location prev = track->locs.front();
  for(size_t ii = 1; ii < track->locs.size(); ++ii) {
    const Location& loc = track->locs[ii];
    double dist = lngLatDist(loc.lngLat(), prev.lngLat());
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
  }

  const Location& loc = track->locs.back();
  std::string posStr = fstring("%.6f, %.6f", loc.lat, loc.lng);
  statsContent->selectFirst(".track-position")->setText(posStr.c_str());

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

  ttot = movingTime;
  hours = int(ttot/3600);
  mins = int((ttot - hours*3600)/60);
  secs = int(ttot - hours*3600 - mins*60);
  statsContent->selectFirst(".track-moving-time")->setText(fstring("%dh %dm %ds", hours, mins, secs).c_str());

  double distUser = app->metricUnits ? trackDist/1000 : trackDist*0.000621371;
  std::string distStr = fstring(app->metricUnits ? "%.2f km" : "%.2f mi", distUser);
  statsContent->selectFirst(".track-dist")->setText(distStr.c_str());

  std::string avgSpeedStr = fstring(app->metricUnits ? "%.2f km/h" : "%.2f mph", distUser/(movingTime/3600));
  statsContent->selectFirst(".track-avg-speed")->setText(avgSpeedStr.c_str());

  std::string ascentStr = app->metricUnits ? fstring("%.0f m", trackAscent) : fstring("%.0f ft", trackAscent*3.28084);
  statsContent->selectFirst(".track-ascent")->setText(ascentStr.c_str());

  std::string ascentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackAscent/(ascentTime/3600))
      : fstring("%.0f ft/h", (trackAscent*3.28084)/(ascentTime/3600));
  statsContent->selectFirst(".track-ascent-speed")->setText(ascentSpdStr.c_str());

  std::string descentStr = app->metricUnits ? fstring("%.0f m", trackDescent) : fstring("%.0f ft", trackDescent*3.28084);
  statsContent->selectFirst(".track-descent")->setText(descentStr.c_str());

  std::string descentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackDescent/(descentTime/3600))
      : fstring("%.0f ft/h", (trackDescent*3.28084)/(descentTime/3600));
  statsContent->selectFirst(".track-descent-speed")->setText(descentSpdStr.c_str());
}

static Widget* createStatsRow(std::vector<const char*> items)  // const char* title1, const char* class1, const char* title2, const char* class2)
{
  Widget* row = createRow();
  for(size_t ii = 0; ii+1 < items.size(); ii += 2) {
    TextBox* label = new TextBox(createTextNode(items[ii]));
    label->node->addClass("weak");
    TextBox* value = new TextBox(createTextNode(""));
    value->node->addClass(items[ii+1]);
    row->addWidget(label);
    row->addWidget(value);
  }

  return row;
}

// Issues:
// - "+" button next to list combo box in place info section to add place to another bookmark list (shows the create bookmark section)
// - "Widget* addWidgets(std::vector<Widget*> widgets) { for(Widget* w : widgets) addWidget(w); return this; }
// - show date for bookmark: with notes and in list
// - track styling: allow setting color in UI (like bookmarks)?  what about line width, dash?
// - option to always record track (i.e. save location history) (while app is in foreground)?
// draw track: we could show distance (length)
// - aside: would it be easier to draw track by moving map and taping button to drop waypoint at map center?
// - we also want option to trace track by dragging finger

MapsTracks::TrackLoc MapsTracks::interpLoc(const TrackLoc& a, const TrackLoc& b, double f)
{
  float g = float(f);
  return TrackLoc{{
      f*b.time + (1-f)*a.time,
      f*b.lat + (1-f)*a.lat,
      f*b.lng + (1-f)*a.lng,
      g*b.poserr + (1-g)*a.poserr,
      f*b.alt + (1-f)*a.alt,
      g*b.alterr + (1-g)*a.alterr,
      g*b.dir + (1-g)*a.dir,
      g*b.direrr + (1-g)*a.direrr,
      g*b.spd + (1-g)*a.spd,
      g*b.spderr + (1-g)*a.spderr
    }, f*b.dist + (1-f)*a.dist};
}

MapsTracks::TrackLoc MapsTracks::interpTrackDist(const std::vector<TrackLoc>& locs, double s, size_t* idxout)
{
  double sd = s*locs.back().dist;
  size_t jj = 0;
  while(locs[jj].dist < sd) ++jj;
  if(idxout) *idxout = jj;
  double f = (sd - locs[jj-1].dist)/(locs[jj].dist - locs[jj-1].dist);
  return interpLoc(locs[jj-1], locs[jj], f);
}

MapsTracks::TrackLoc MapsTracks::interpTrackTime(const std::vector<TrackLoc>& locs, double s, size_t* idxout)
{
  double st = s*locs.back().time + (1-s)*locs.front().time;
  size_t jj = 0;
  while(locs[jj].time < st) ++jj;
  if(idxout) *idxout = jj;
  double f = (st - locs[jj-1].time)/(locs[jj].time - locs[jj-1].time);
  return interpLoc(locs[jj-1], locs[jj], f);
}

MapsTracks::TrackLoc MapsTracks::interpTrack(const std::vector<TrackLoc>& locs, double s, size_t* idxout)
{
  return trackPlotDist ? interpTrackDist(locs, s, idxout) : interpTrackTime(locs, s, idxout);
}


Widget* MapsTracks::createPanel()
{
  static const char* trackListProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g layout="flex" flex-direction="row" box-anchor="left">
        <g class="image-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_drawer.svg"/>
        </g>
        <g layout="box" box-anchor="vfill">
          <text class="title-text" box-anchor="left" margin="0 10"></text>
          <text class="detail-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
        </g>

        <g class="toolbutton visibility-btn" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_pin.svg"/>
        </g>

        <g class="toolbutton overflow-btn" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_overflow.svg"/>
        </g>

      </g>
    </g>
  )";
  trackListProto.reset(loadSVGFragment(trackListProtoSVG));

  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS tracks(title TEXT, filename TEXT, style TEXT, timestamp INTEGER DEFAULT CAST(strftime('%s') AS INTEGER), archived INTEGER DEFAULT 0);");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS tracks_state(track_id INTEGER, order INTEGER, visible INTEGER);");

  // Stats panel
  statsContent = createColumn();

  Widget* saveTrackContent = createColumn();
  TextEdit* saveTitle = createTextEdit();
  TextEdit* savePath = createTextEdit();
  Button* saveTrackBtn = createPushbutton("Apply");
  saveTrackBtn->onClicked = [=](){
    // save under new filename, then delete old file
    std::string prevFile = recordedTrack.gpxFile;
    recordedTrack.title = saveTitle->text();
    recordedTrack.gpxFile = savePath->text();
    if(saveGPX(&recordedTrack)) {
      removeFile(prevFile);
      saveTrackContent->setVisible(false);
    }
    else
      recordedTrack.gpxFile = prevFile;
  };
  // TODO: also include track update interval
  saveTrackContent->addWidget(createTitledRow("Title", saveTitle));
  saveTrackContent->addWidget(createTitledRow("File", savePath));
  saveTrackContent->addWidget(saveTrackBtn);
  saveTrackContent->setVisible(false);
  statsContent->addWidget(saveTrackContent);

  // bearing? direction (of travel)?
  statsContent->addWidget(createStatsRow({"Position", "track-position", "Altitude", "track-altitude", "Speed", "track-speed"}));
  statsContent->addWidget(createStatsRow({"Total time", "track-time", "Moving time", "track-moving-time"}));
  statsContent->addWidget(createStatsRow({"Distance", "track-dist", "Avg speed", "track-avg-speed"}));
  statsContent->addWidget(createStatsRow({"Ascent", "track-ascent", "Descent", "track-descent"}));
  statsContent->addWidget(createStatsRow({"Ascent speed", "track-ascent-speed", "Descent speed", "track-descent-speed"}));

  trackPlot = new TrackPlot();
  trackPlot->node->setAttribute("box-anchor", "fill");
  trackPlot->setMargins(1, 5);

  statsContent->addWidget(trackPlot);

  TrackSliders* trackSliders = createTrackSliders();
  statsContent->addWidget(trackSliders);

  trackSliders->onCropHandlesChanged = [=](){
    TrackLoc startloc = interpTrack(activeTrack->locs, trackSliders->startHandlePos);
    if(trackStartMarker == 0) {
      trackStartMarker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(trackStartMarker, "layers.track-marker.draw.marker");
    }
    app->map->markerSetVisible(trackStartMarker, true);
    app->map->markerSetPoint(trackStartMarker, startloc.lngLat());

    TrackLoc endloc = interpTrack(activeTrack->locs, trackSliders->endHandlePos);
    if(trackEndMarker == 0) {
      trackEndMarker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(trackEndMarker, "layers.track-marker.draw.marker");
    }
    app->map->markerSetVisible(trackEndMarker, true);
    app->map->markerSetPoint(trackEndMarker, endloc.lngLat());
  };

  Button* cropTrackBtn = createToolbutton(NULL, "Crop to segment", true);
  Button* appendTrackBtn = createToolbutton(NULL, "Append track", true);
  Button* moreTrackOptionsBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_overflow.svg"), "More options");

  Menu* trackPlotOverflow = createMenu(Menu::VERT_LEFT);
  trackPlotOverflow->addItem("Reverse Track", [this](){
    std::reverse(activeTrack->locs.begin(), activeTrack->locs.end());
    populateStats(activeTrack);
  });

  trackPlotOverflow->addItem("Delete Segment", [=](){
    const std::vector<TrackLoc>& locs = activeTrack->locs;
    std::vector<TrackLoc> newlocs;
    size_t restartidx, endidx;
    auto endloc = interpTrack(locs, trackSliders->startHandlePos, &endidx));
    auto restartloc = interpTrack(locs, trackSliders->endHandlePos, &restartidx);
    newlocs.insert(newlocs.end(), locs.begin(), locs.begin() + endidx);
    newlocs.push_back(endloc);
    newlocs.push_back(restartloc);
    newlocs.insert(newlocs.end(), locs.begin() + restartidx, locs.end());
    activeTrack->locs.swap(newlocs);
    trackSliders->startHandlePos = 0;
    trackSliders->endHandlePos = 1;
    populateStats(activeTrack);
  });

  moreTrackOptionsBtn->setMenu(trackPlotOverflow);

  Toolbar* trackOptionsTb = createToolbar();
  trackOptionsTb->addWidget(cropTrackBtn);
  trackOptionsTb->addWidget(appendTrackBtn);
  trackOptionsTb->addWidget(moreTrackOptionsBtn);

  statsContent->addWidget(trackOptionsTb);

  // controlling what's plotted:
  // speed, altitude vs. time, dist ... tap on axis to show selector?

  // marker locations could be in distance or time!
  cropTrackBtn->onClicked = [=](){
    const std::vector<TrackLoc>& locs = activeTrack->locs;
    std::vector<TrackLoc> newlocs;
    size_t startidx, endidx;
    newlocs.push_back(interpTrack(locs, trackSliders->startHandlePos, &startidx));
    auto endloc = interpTrack(locs, trackSliders->endHandlePos, &endidx);
    newlocs.insert(newlocs.end(), locs.begin() + startidx, locs.begin() + endidx);
    newlocs.push_back(endloc);
    activeTrack->locs.swap(newlocs);
    trackSliders->startHandlePos = 0;
    trackSliders->endHandlePos = 1;
    populateStats(activeTrack);
  };

  appendTrackBtn->onClicked = [this](){
    if(!selectTrackDialog) {
      selectTrackDialog.reset(createSelectDialog("Choose Track", SvgGui::useFile(":/icons/ic_menu_select_path.svg")));
      selectTrackDialog->onSelected = [this](int idx){
        activeTrack->locs.insert(activeTrack->locs.end(), tracks[idx].locs.begin(), tracks[idx].locs.end());
        populateStats(activeTrack);
      };
    }
    selectTrackDialog->addItems({});
    for(auto& track : tracks)
      selectTrackDialog->addItems({track.title});
    MapsApp::gui->showModal(selectTrackDialog.get(), MapsApp::gui->windows.front()->modalOrSelf());
  };

  trackPlot->onHovered = [this](real s){
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

  Button* editTrackBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_draw.svg"), "Edit Track");
  editTrackBtn->onClicked = [=](){
    saveTrackContent->setVisible(!saveTrackContent->isVisible());
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
    recordTrack = !recordTrack;
    if(recordTrack) {
      char timestr[64];
      time_t t = mSecSinceEpoch();
      strftime(timestr, sizeof(timestr), "%FT%T", localtime(&t));  //"%Y-%m-%d %HH%M"
      recordedTrack = Track{timestr, "", std::string(timestr) + ".gpx", 0, {}, -1, true, false};
      recordedTrack.marker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(recordedTrack.marker, "layers.track.draw.recorded-track");
      saveTrackContent->setVisible(true);
      populateTracks(false);
      populateStats(&recordedTrack);
    }
    else {
      saveGPX(&recordedTrack);
      const char* query = "INSERT INTO tracks (title,filename) VALUES (?,?);";
      DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
        sqlite3_bind_text(stmt, 1, recordedTrack.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, recordedTrack.gpxFile.c_str(), -1, SQLITE_TRANSIENT);
      });
      recordedTrack.rowid = sqlite3_last_insert_rowid(app->bkmkDB);
      tracks.push_back(std::move(recordedTrack));
      recordedTrack = Track{};
      populateTracks(false);
    }
  };

  TextEdit* gpxPath = createTextEdit();
  tracksContent->addWidget(createTitledRow("GPX File", gpxPath));

  Button* addGpxBtn = createPushbutton("Add");

  addGpxBtn->onClicked = [=](){
    auto track = loadGPX(gpxPath->text().c_str());
    if(!track.locs.empty()) {
      const char* query = "INSERT INTO tracks (title,filename) VALUES (?,?);";
      DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
        sqlite3_bind_text(stmt, 1, track.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, track.gpxFile.c_str(), -1, SQLITE_TRANSIENT);
      });
      tracks.push_back(std::move(track));
      populateTracks(false);
      populateStats(&tracks.back());
      loadTrackPanel->setVisible(false);
    }
  };

  Widget* btnRow = createRow();
  btnRow->addWidget(addGpxBtn);
  //btnRow->addWidget(replaceGpxBtn);
  loadTrackPanel->addWidget(btnRow);
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
      tracksDirty = false;
    }
    return false;
  });

  archivedContent = createColumn();
  auto archivedHeader = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Archived Tracks");
  archivedPanel = app->createMapPanel(archivedHeader, archivedContent);

  auto statsTb = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_bookmark.svg"), "");
  statsTb->addWidget(editTrackBtn);
  statsPanel = app->createMapPanel(statsTb, statsContent);

  statsPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::INVISIBLE) {
      if(activeTrack != &recordedTrack)
        app->map->markerSetStylingFromPath(activeTrack->marker, "layers.track-marker.draw.marker");
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
