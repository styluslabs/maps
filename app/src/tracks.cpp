#include "tracks.h"
#include "mapsapp.h"
#include "util.h"
#include "mapwidgets.h"
#include "plugins.h"
#include "mapsearch.h"
#include "bookmarks.h"
#include "trackwidgets.h"

#include "yaml-cpp/yaml.h"
#include "sqlitepp.h"
#include "util/yamlPath.h"
#include "util/mapProjection.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "usvg/svgpainter.h"
#include "usvg/svgparser.h"  // for parseColor
#include "usvg/svgwriter.h"  // for serializeColor


// decode encoded polyline, used by Valhalla, Google, etc.
// - https://github.com/valhalla/valhalla/blob/master/docs/docs/decoding.md - Valhalla uses 1E6 precision
// - https://developers.google.com/maps/documentation/utilities/polylinealgorithm - Google uses 1E5 precision
std::vector<Waypoint> MapsTracks::decodePolylineStr(const std::string& encoded, double precision)
{
  const double invprec = 1/precision;
  size_t i = 0;     // what byte are we looking at

  // Handy lambda to turn a few bytes of an encoded string into an integer
  auto deserialize = [&encoded, &i](const int previous) {
    // Grab each 5 bits and mask it in where it belongs using the shift
    int byte, shift = 0, result = 0;
    do {
      byte = static_cast<int>(encoded[i++]) - 63;
      result |= (byte & 0x1f) << shift;
      shift += 5;
    } while (byte >= 0x20);
    // Undo the left shift from above or the bit flipping and add to previous since it's an offset
    return previous + (result & 1 ? ~(result >> 1) : (result >> 1));
  };

  // Iterate over all characters in the encoded string
  std::vector<Waypoint> shape;
  int lon = 0, lat = 0;
  while (i < encoded.length()) {
    lat = deserialize(lat);
    lon = deserialize(lon);
    shape.emplace_back(LngLat(lon*invprec, lat*invprec));
  }
  return shape;
}

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
      if(activeTrack == &recordedTrack) {
        updateStats(activeTrack);
        // if zoomed and scrolled to end of plot, scroll to include new location point
        if(trackPlot->zoomScale > 1 && trackPlot->zoomOffset == trackPlot->minOffset)
          trackPlot->zoomOffset = -INFINITY;
        trackPlot->setTrack(recordedTrack.tracks.back().pts, recordedTrack.waypoints);
        recordedTrack.desc = "Recording" + trackSummary;
      }
      if(loc.time > recordLastSave + 60) {
        saveGPX(&recordedTrack);
        recordLastSave = loc.time;
      }
    }
  }
}

static void addRouteStepMarker(Map* map, Waypoint& wp, GpxFile* track)
{
  if(wp.marker <= 0) {
    wp.marker = map->markerAdd();
    map->markerSetStylingFromPath(wp.marker, "layers.route-step.draw.marker");
    map->markerSetPoint(wp.marker, wp.loc.lngLat());
  }
  // use marker number as unique id for priority
  map->markerSetProperties(wp.marker,
      {{{"name", wp.desc}, {"color", track->style}, {"priority", wp.marker}}});
}

void MapsTracks::updateTrackMarker(GpxFile* track)
{
  if(!track->loaded && !track->filename.empty())
    loadGPX(track);

  if(!track->marker)
    track->marker = std::make_unique<TrackMarker>(app->map.get(), "layers.track.draw.track");
  if(track->activeWay() && (track->activeWay()->pts.size() > 1 || track->routes.size() > 1)) {
    if(!track->style.empty())
      track->marker->markerProps = {{{"color", track->style}}};
    if(!track->routes.empty())
      track->marker->setTrack(&track->routes.front(), track->routes.size());
    else
      track->marker->setTrack(&track->tracks.front(), track->tracks.size());
    track->marker->setVisible(true);
  }
  else
    track->marker->setVisible(false);

  if(!track->routes.empty() && track->routeMode != "direct") {
    auto& pts = track->routes.back().pts;
    for(size_t ii = 1; ii < pts.size(); ++ii) {
      if(!pts[ii].desc.empty())  // && wp.marker <= 0)
        addRouteStepMarker(app->map.get(), pts[ii], track);
    }
  }

  for(Waypoint& wp : track->waypoints) {
    if(wp.marker <= 0) {
      wp.marker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(wp.marker, "layers.waypoint.draw.marker");
      app->map->markerSetPoint(wp.marker, wp.loc.lngLat());
    }
    // use marker number as unique id for priority
    app->map->markerSetProperties(wp.marker,
        {{{"name", wp.name}, {"color", track->style}, {"priority", wp.marker}}});
  }
}

void MapsTracks::showTrack(GpxFile* track, bool show)  //, const char* styling)
{
  if(!track->marker) {
    if(!show) return;
    updateTrackMarker(track);
  }
  track->marker->setVisible(show && track->activeWay() && track->activeWay()->pts.size() > 1);
  for(Waypoint& wp : track->waypoints)
    app->map->markerSetVisible(wp.marker, show);

  for(GpxWay& route : track->routes) {
    for(Waypoint& wp : route.pts) {
      if(wp.marker > 0)
        app->map->markerSetVisible(wp.marker, show);
    }
  }
}

void MapsTracks::setTrackVisible(GpxFile* track, bool visible)
{
  track->visible = visible;
  if(visible && track->rowid >= 0)
    app->config["tracks"]["visible"].push_back(track->rowid);
  else
    yamlRemove(app->config["tracks"]["visible"], track->rowid);
  // assume recordedTrack is dirty (should we introduce a Track.dirty flag?)
  if(visible && track == &recordedTrack)
    updateTrackMarker(track);
  showTrack(track, visible);  //, "layers.track.draw.track");
  // to handle toggle from quick menu ... general soln to this issue is to use Actions, but there are only
  //   a few cases like this in this app, so we don't bother
  for(Widget* item : tracksContent->select(".listitem")) {
    if(item->node->getIntAttr("__rowid", INT_MAX) == track->rowid)
      static_cast<Button*>(item->selectFirst(".show-btn"))->setChecked(visible);
  }
}

Widget* MapsTracks::createTrackEntry(GpxFile* track)
{
  Button* item = createListItem(MapsApp::uiIcon("track"), track->title.c_str(), track->desc.c_str());
  item->node->setAttr("__rowid", track->rowid);
  item->onClicked = [=](){
    // make sure track is loaded so we can decide between stats and waypoints
    if(track->marker <= 0)
      updateTrackMarker(track);
    trackSliders->setValue(0);  // reset track slider for new track
    populateTrack(track);
  };
  Widget* container = item->selectFirst(".child-container");

  Button* showBtn = createToolbutton(MapsApp::uiIcon("eye"), "Show");
  showBtn->node->addClass("show-btn");
  showBtn->onClicked = [=](){
    setTrackVisible(track, !track->visible);
    showBtn->setChecked(track->visible);  // toggle in setTrackVisible() only handles non-archived case
  };
  showBtn->setChecked(track->visible);
  container->addWidget(showBtn);

  ColorPicker* colorBtn = createColorPicker(app->colorPickerMenu, parseColor(track->style, Color::BLUE));
  colorBtn->onColor = [this, track](Color color){
    std::string colorstr = colorToStr(color);
    track->style = colorstr;
    if(track->marker) {
      updateTrackMarker(track);
      showTrack(track, track->visible);
    }
    if(track->rowid >= 0)
      SQLiteStmt(app->bkmkDB, "UPDATE tracks SET style = ? WHERE rowid = ?;").bind(colorstr, track->rowid).exec();
  };
  container->addWidget(colorBtn);

  Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
  Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
  if(track == &recordedTrack) {  //->rowid >= 0) {
    overflowMenu->addItem("Pause", [this](){ pauseRecordBtn->onClicked(); });
    overflowMenu->addItem("Stop", [this](){ stopRecordBtn->onClicked(); });
  }
  else {
    overflowMenu->addItem(track->archived ? "Unarchive" : "Archive", [=](){
      std::string q2 = fstring("UPDATE tracks SET archived = %d WHERE rowid = ?;", track->archived ? 0 : 1);
      SQLiteStmt(app->bkmkDB, q2).bind(track->rowid).exec();
      track->archived = !track->archived;
      if(!track->archived) tracksDirty = true;
      else archiveDirty = true;
      app->gui->deleteWidget(item);  // must be last!
    });

    overflowMenu->addItem("Delete", [=](){
      SQLiteStmt(app->bkmkDB, "DELETE FROM tracks WHERE rowid = ?").bind(track->rowid).exec();
      for(auto it = tracks.begin(); it != tracks.end(); ++it) {
        if(it->rowid == track->rowid) {
          // move GPX file to trash and add undelete item
          FSPath fileinfo(track->filename);
          FSPath trashinfo(MapsApp::baseDir, ".trash/" + fileinfo.fileName());
          moveFile(fileinfo, trashinfo);
          app->addUndeleteItem(track->title, MapsApp::uiIcon("track"), [=](){
            GpxFile restored("", "", trashinfo.path);
            loadGPX(&restored);
            tracks.push_back(std::move(restored));
            updateDB(&tracks.back());
            populateTrackList();
          });
          bool archived = track->archived;
          yamlRemove(app->config["tracks"]["visible"], track->rowid);
          tracks.erase(it);
          app->gui->deleteWidget(item);
          break;
        }
      }
    });
  }
  overflowBtn->setMenu(overflowMenu);
  container->addWidget(overflowBtn);
  return item;
}

void MapsTracks::loadTracks(bool archived)
{
  // order by timestamp for Archived
  const char* query = "SELECT rowid, title, filename, strftime('%Y-%m-%d', timestamp, 'unixepoch'), style"
      " FROM tracks WHERE archived = ? ORDER BY timestamp;";
  SQLiteStmt(app->bkmkDB, query).bind(archived).exec(
      [&](int rowid, std::string title, std::string filename, std::string date, std::string style) {
    tracks.emplace_back(title, date, filename, style, rowid, archived);
  });
}

void MapsTracks::populateArchived()
{
  if(!archiveLoaded)
    loadTracks(true);
  else if(!archiveDirty)
    return;
  archiveLoaded = true;
  archiveDirty = false;
  // needed to handle case of archiving an imported track, maybe some other edge cases
  tracks.sort([](const GpxFile& a, const GpxFile& b){ return a.timestamp < b.timestamp; });

  app->gui->deleteContents(archivedContent, ".listitem");
  // tracks is ordered by ascending timestamp, but we want Archived ordered from newest to oldest
  for(auto it = tracks.rbegin(); it != tracks.rend(); ++it) {  //for(GpxFile& track : tracks) {
    if(it->archived)
      archivedContent->addWidget(createTrackEntry(&(*it)));
  }
}

void MapsTracks::populateTrackList()
{
  tracksDirty = false;
  std::vector<std::string> order = tracksContent->getOrder();
  if(order.empty()) {
    for(const auto& key : app->config["tracks"]["list_order"])
      order.push_back(key.Scalar());
  }
  tracksContent->clear();

  if(!recordedTrack.tracks.empty())
    tracksContent->addItem("rec", createTrackEntry(&recordedTrack));
  for(GpxFile& track : tracks) {
    if(!track.archived)
      tracksContent->addItem(std::to_string(track.rowid), createTrackEntry(&track));
  }
  Button* item = createListItem(MapsApp::uiIcon("archive"), "Archived Tracks");
  item->onClicked = [this](){ app->showPanel(archivedPanel, true);  populateArchived(); };
  tracksContent->addItem("archived", item);
  tracksContent->setOrder(order);
}

void MapsTracks::viewEntireTrack(GpxFile* track)
{
  if(!track->activeWay() || track->activeWay()->pts.empty()) return;
  auto& locs = track->activeWay()->pts;
  LngLat minLngLat(locs.front().lngLat()), maxLngLat(locs.front().lngLat());
  for(size_t ii = 1; ii < locs.size(); ++ii) {
    Location& loc = locs[ii].loc;
    minLngLat.longitude = std::min(minLngLat.longitude, loc.lng);
    minLngLat.latitude = std::min(minLngLat.latitude, loc.lat);
    maxLngLat.longitude = std::max(maxLngLat.longitude, loc.lng);
    maxLngLat.latitude = std::max(maxLngLat.latitude, loc.lat);
  }
  if(!app->map->lngLatToScreenPosition(minLngLat.longitude, minLngLat.latitude)
      || !app->map->lngLatToScreenPosition(maxLngLat.longitude, maxLngLat.latitude)) {
    auto newpos = app->map->getEnclosingCameraPosition(minLngLat, maxLngLat, {32});
    newpos.zoom = std::min(newpos.zoom, 17.0f);
    app->map->setCameraPositionEased(newpos, 0.5f);
  }
}

void MapsTracks::setTrackWidgets(TrackView_t view)
{
  // must hide before showing!
  if(view != TRACK_STATS) { for(Widget* w : statsWidgets) w->setVisible(false); }
  if(view != TRACK_PLOT) { for(Widget* w : plotWidgets) w->setVisible(false); }
  if(view != TRACK_WAYPTS) { for(Widget* w : wayptWidgets) w->setVisible(false); }
  if(view == TRACK_STATS) { for(Widget* w : statsWidgets) w->setVisible(true); }
  if(view == TRACK_PLOT) { for(Widget* w : plotWidgets) w->setVisible(true); }
  if(view == TRACK_WAYPTS) { for(Widget* w : wayptWidgets) w->setVisible(true); }
}

void MapsTracks::populateTrack(GpxFile* track)  //TrackView_t view)
{
  bool istrack = track->routes.empty() && !track->tracks.empty();
  app->showPanel(trackPanel, true);
  static_cast<TextLabel*>(trackPanel->selectFirst(".panel-title"))->setText(track->title.c_str());

  showTrack(track, true);
  bool isRecTrack = track == &recordedTrack;
  pauseRecordBtn->setVisible(isRecTrack);
  stopRecordBtn->setVisible(isRecTrack);
  saveCurrLocBtn->setVisible(isRecTrack);

  if(activeTrack != track) {
    viewEntireTrack(track);
    if(!isRecTrack)
      track->marker->setStylePath("layers.selected-track.draw.track");
    insertionWpt.clear();
    activeTrack = track;
    retryBtn->setVisible(false);
    //if(!track->activeWay())
    //  sparkStats->setVisible(false);
    if(!istrack && !wayptWidgets[0]->isVisible())
      setTrackWidgets(TRACK_WAYPTS);
    if(istrack && wayptWidgets[0]->isVisible())
      setTrackWidgets(TRACK_PLOT);
  }
  trackPlot->zoomScale = 1.0;
  updateStats(track);

  if(wayptWidgets[0]->isVisible()) {
    waypointsDirty = false;
    if(activeTrack != track) {
      if(!istrack && track->routeMode.empty())
        track->routeMode = "direct";
      setRouteMode(track->routeMode);
    }
    wayptContent->clear();
    for(Waypoint& wp : track->waypoints) {
      if(!wp.name.empty() || showAllWaypts || istrack)
        addWaypointItem(wp);
    }
    updateDistances();

    // for now we'll try to make routes and tracks mutually exclusive
    saveRouteBtn->setVisible(!istrack && track->filename.empty());
    routeModeBtn->setVisible(!istrack);
    routePluginBtn->setVisible(!istrack);
  }
  else
    waypointsDirty = true;
}

static std::string durationToStr(double totsecs)
{
  int hours = int(totsecs/3600);
  int mins = int((totsecs - hours*3600)/60);
  int secs = int(totsecs - hours*3600 - mins*60);
  return fstring("%02d:%02d:%02d", hours, mins, secs);  //fstring("%dh %02dm %02ds", hours, mins, secs);
}

void MapsTracks::setStatsText(const char* selector, std::string str)
{
  Widget* widget = statsContent->selectFirst(selector);
  if(!widget || widget->node->type() != SvgNode::TEXT) return;
  char* value = str.data();
  char* units = strchr(value, ' ');
  if(units) *units++ = '\0';
  static_cast<SvgTspan*>(widget->node->selectFirst(".stat-value-tspan"))->setText(value);
  static_cast<SvgTspan*>(widget->node->selectFirst(".stat-units-tspan"))->setText(units ? units : "");
}

void MapsTracks::updateStats(GpxFile* track)
{
  static const char* notime = u8"\u2014";  // emdash
  if(!track->activeWay()) return;
  std::vector<Waypoint>& locs = track->activeWay()->pts;
  if(!locs.empty())
    locs.front().dist = 0;
  // how to calculate max speed?
  double trackDist = 0, trackAscent = 0, trackDescent = 0, ascentTime = 0, descentTime = 0, movingTime = 0;
  double currSpeed = 0, maxSpeed = 0, currSlope = 0;
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
      if(dist > 0)
        currSlope = a*currSlope + (1-a)*vert/dist;
    }

    trackDist += dist;
    trackAscent += std::max(0.0, vert);
    trackDescent += std::min(0.0, vert);
    ascentTime += vert > 0 ? dt : 0;
    descentTime += vert < 0 ? dt : 0;

    //if(!origLocs.empty())  // track has been modified - recalc distances
    locs[ii].dist = trackDist;
  }

  const Location& loc = recordTrack ? app->currLocation : trackHoverLoc.loc;
  liveStatsRow->setVisible(recordTrack);
  nonliveStatsRow->setVisible(!recordTrack);

  statsContent->selectFirst(".track-start-date")->setText(ftimestr("%F %H:%M:%S", locs.front().loc.time*1000).c_str());
  statsContent->selectFirst(".track-end-date")->setText(ftimestr("%F %H:%M:%S", locs.back().loc.time*1000).c_str());

  setStatsText(".track-altitude", app->elevToStr(loc.alt));

  // m/s -> kph or mph
  setStatsText(".track-speed",
      app->metricUnits ? fstring("%.2f km/h", currSpeed*3.6) : fstring("%.2f mph", currSpeed*2.23694));

  double slopeDeg = std::atan(currSlope)*180/M_PI;
  setStatsText(".track-slope",
      fstring("%.*f%% (%.*f\00B0)", currSlope < 0.1 ? 1 : 0, currSlope*100, slopeDeg < 10 ? 1 : 0, slopeDeg));

  setStatsText(".track-direction", fstring("%.0f\00B0", loc.dir));

  double ttot = locs.empty() ? 0 : locs.back().loc.time - locs.front().loc.time;
  auto timeStr = durationToStr(ttot);
  sparkStats->selectFirst(".spark-time")->setText(timeStr.c_str());
  setStatsText(".track-time", timeStr);
  setStatsText(".track-moving-time", durationToStr(movingTime));

  std::string distStr = MapsApp::distKmToStr(trackDist/1000);
  sparkStats->selectFirst(".spark-dist")->setText(distStr.c_str());
  setStatsText(".track-dist", distStr);

  double distUser = app->metricUnits ? trackDist/1000 : trackDist*0.000621371;
  std::string avgSpeedStr = fstring(app->metricUnits ? "%.2f km/h" : "%.2f mph", distUser/(movingTime/3600));
  setStatsText(".track-avg-speed", movingTime > 0 ? avgSpeedStr : notime);

  std::string ascentStr = app->elevToStr(trackAscent);
  sparkStats->selectFirst(".spark-ascent")->setText(("+" + ascentStr).c_str());
  setStatsText(".track-ascent", ascentStr);

  std::string ascentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackAscent/(ascentTime/3600))
      : fstring("%.0f ft/h", (trackAscent*3.28084)/(ascentTime/3600));
  setStatsText(".track-ascent-speed", ascentTime > 0 ? ascentSpdStr : notime);

  std::string descentStr = app->elevToStr(trackDescent);  //app->metricUnits ? fstring("%.0f m", trackDescent) : fstring("%.0f ft", trackDescent*3.28084);
  sparkStats->selectFirst(".spark-descent")->setText(descentStr.c_str());
  setStatsText(".track-descent", descentStr);

  std::string descentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackDescent/(descentTime/3600))
      : fstring("%.0f ft/h", (trackDescent*3.28084)/(descentTime/3600));
  setStatsText(".track-descent-speed", descentTime > 0 ? descentSpdStr : notime);

  trackSummary = " | " + timeStr + " | " + distStr;
  sparkStats->setVisible(locs.size() > 1);
  trackSpark->setTrack(locs);
  plotVsTimeBtn->setVisible(ttot > 0);

  //if(plotWidgets[0]->isVisible())
  trackPlot->setTrack(locs, track->waypoints);  //else plotDirty = true;
}

void MapsTracks::updateDistances()
{
  if(!activeTrack || activeTrack->routes.empty()) return;
  auto& route = activeTrack->routes.back().pts;
  if(route.empty()) return;
  auto wayPtItems = wayptContent->select(".listitem");  //->content->containerNode()->children();

  if(activeTrack->routeMode == "direct") {
    size_t rteidx = 0;
    for(Widget* item : wayPtItems) {
      auto it = activeTrack->findWaypoint(item->node->getStringAttr("__sortkey"));
      if(!it->routed) continue;
      while(rteidx < route.size()-1 && route[rteidx].lngLat() != it->lngLat()) ++rteidx;
      TextLabel* detail = static_cast<TextLabel*>(item->selectFirst(".detail-text"));
      std::string s = MapsApp::distKmToStr(route[rteidx].dist/1000);
      if(!it->desc.empty())
        s.append(u8" \u2022 ").append(it->desc);
      detail->setText(s.c_str());
      it->dist = route[rteidx].dist;  // for stats plot
    }
    return;
  }

  double xmin = DBL_MAX, ymin = DBL_MAX, xmax = DBL_MIN, ymax = DBL_MIN;
  for(const Waypoint& rtept : route) {
    auto r = MapProjection::lngLatToProjectedMeters(rtept.lngLat());
    xmin = std::min(xmin, r.x);
    ymin = std::min(ymin, r.y);
    xmax = std::max(xmax, r.x);
    ymax = std::max(ymax, r.y);
  }

  isect2d::ISect2D<glm::vec2> collider;
  collider.resize({64, 64}, {xmax-xmin, ymax-ymin});
  Tangram::ProjectedMeters r0(xmin, ymin);
  for(const Waypoint& rtept : route) {
    auto r = MapProjection::lngLatToProjectedMeters(rtept.lngLat()) - r0;
    isect2d::AABB<glm::vec2> aabb(r.x, r.y, r.x, r.y);
    aabb.m_userData = (void*)(&rtept);
    collider.insert(aabb);
  }

  glm::dvec2 radius{(xmax-xmin)/64, (ymax-ymin)/64};
  for(Widget* item : wayPtItems) {
    float dist = FLT_MAX;
    Waypoint* rtePt = NULL;
    auto it = activeTrack->findWaypoint(item->node->getStringAttr("__sortkey"));
    auto r = MapProjection::lngLatToProjectedMeters(it->lngLat()) - r0;
    isect2d::AABB<glm::vec2> aabb(r.x - radius.x, r.y - radius.y, r.x + radius.x, r.y + radius.y);
    collider.intersect(aabb, [&](auto& a, auto& b) {
      float d = glm::distance(a.getCentroid(), b.getCentroid());
      if(d < dist) {
        dist = d;
        rtePt = (Waypoint*)(b.m_userData);
      }
      return true;
    }, false);

    if(!rtePt) continue;  // should never happen
    double distkm = rtePt->dist/1000 + lngLatDist(rtePt->lngLat(), it->lngLat());
    TextLabel* detail = static_cast<TextLabel*>(item->selectFirst(".detail-text"));
    std::string s = MapsApp::distKmToStr(distkm);
    if(!it->desc.empty())
      s.append(u8" \u2022 ").append(it->desc);  // or \u00B7
    detail->setText(s.c_str());
    it->dist = rtePt->dist;  // for stats plot
  }
}

void MapsTracks::createRoute(GpxFile* track)
{
  if(track->routes.empty() && !track->tracks.empty()) return;  // don't add route if track already present
  retryBtn->setVisible(false);
  track->routes.clear();
  track->modified = true;
  if(track->routeMode == "draw") {}
  else if(track->routeMode == "direct") {
    track->routes.emplace_back();
    for(Waypoint& wp : track->waypoints) {
      if(wp.routed)
        track->routes.back().pts.push_back(wp.loc);
    }
    updateStats(track);
    updateDistances();
    updateTrackMarker(track);
  }
  else {
    std::vector<LngLat> pts;
    for(Waypoint& wp : track->waypoints) {
      if(wp.routed)
        pts.push_back(wp.loc.lngLat());
    }
    if(pts.size() > 1)
      app->pluginManager->jsRoute(pluginFn, track->routeMode, pts);
  }
}

// supporting multiple routes: std::list<std::vector<Waypoint>> altRoutes;
void MapsTracks::addRoute(std::vector<Waypoint>&& route)
{
  if(!activeTrack) return;
  activeTrack->routes.emplace_back();
  activeTrack->routes.back().pts = std::move(route);
  activeTrack->modified = true;
  updateStats(activeTrack);
  updateDistances();
  updateTrackMarker(activeTrack);
}

// in the future, we can add support for lat,lng instead of wayptidx and find the nearest route pt
void MapsTracks::addRouteStep(const char* instr, int rteptidx)
{
  if(!activeTrack || activeTrack->routes.empty() || activeTrack->routes[0].pts.size() <= rteptidx || rteptidx < 0) return;
  Waypoint& wp = activeTrack->routes[0].pts[rteptidx];
  wp.desc = instr;
  addRouteStepMarker(app->map.get(), wp, activeTrack);
}

void MapsTracks::routePluginError(const char* err)
{
  retryBtn->setVisible(true);
}

void MapsTracks::removeWaypoint(GpxFile* track, const std::string& uid)
{
  if(!track) return;  // should never happen
  auto it = track->findWaypoint(uid);
  if(it == track->waypoints.end()) return;  // also should never happen
  bool routed = it->routed;
  wayptContent->deleteItem(uid);
  if(track == activeTrack && it->uid == insertionWpt)
    insertionWpt.clear();
  track->waypoints.erase(it);
  track->modified = true;
  if(track->waypoints.empty())
    app->crossHair->routePreviewOrigin = {NAN, NAN};  //app->map->markerSetVisible(previewMarker, false);
  if(routed)
    createRoute(track);
  wayptTabLabel->setText(fstring("%d waypoints", int(activeTrack->waypoints.size())).c_str());
}

void MapsTracks::editWaypoint(GpxFile* track, const Waypoint& wpt, std::function<void()> callback)
{
  std::string uid = wpt.uid;
  auto titleEdit = createTitledTextEdit("Name");
  auto noteEdit = createTitledTextEdit("Note");
  titleEdit->setText(wpt.name.c_str());
  noteEdit->setText(wpt.desc.c_str());

  auto onAcceptEdit = [=](){
    auto it = track->findWaypoint(uid);
    it->name = titleEdit->text();
    it->desc = noteEdit->text();
    if(it->marker) {
      app->map->markerSetProperties(it->marker,
          {{{"name", it->name}, {"color", track->style}, {"priority", it->marker}}});
    }
    waypointsDirty = true;
    // hack around problem of setPickResult destroying this closure
    //refreshWayptPlaceInfo(track, *it);
    if(callback) callback();  //track, *it);
  };
  //auto editContent = createInlineDialog({titleEdit, noteEdit}, "Apply", onAcceptEdit, onCancelEdit);
  editWayptDialog.reset(createInputDialog({titleEdit, noteEdit}, "Edit Waypoint", "Apply", onAcceptEdit));
  showModalCentered(editWayptDialog.get(), app->gui);  //showInlineDialogModal(editContent);
  app->gui->setFocused(titleEdit->text().empty() ? titleEdit : noteEdit, SvgGui::REASON_TAB);
}

// cut and paste from bookmarks
void MapsTracks::setPlaceInfoSection(GpxFile* track, const Waypoint& wpt)
{
  std::string uid = wpt.uid;
  Widget* section = createColumn();
  section->node->setAttribute("box-anchor", "hfill");
  TextBox* noteText = new TextBox(loadSVGFragment(
      R"(<text class="note-text weak" box-anchor="left" margin="0 10" font-size="12"></text>)"));
  noteText->setText(wpt.desc.c_str());
  noteText->setText(SvgPainter::breakText(static_cast<SvgText*>(noteText->node), 250).c_str());

  Widget* toolRow = createRow();
  Button* chooseListBtn = createToolbutton(MapsApp::uiIcon("waypoint"), track->title.c_str(), true);
  Button* removeBtn = createToolbutton(MapsApp::uiIcon("discard"), "Delete");
  Button* addNoteBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit");

  chooseListBtn->onClicked = [=](){ populateTrack(track); };

  removeBtn->onClicked = [=](){
    removeWaypoint(track, uid);
    section->setVisible(false);
  };

  addNoteBtn->onClicked = [=](){
    auto& wp = *track->findWaypoint(uid);
    editWaypoint(track, wp, [this, track, &wp](){
      app->setPickResult(app->pickResultCoord, wp.name, app->pickResultProps);
      setPlaceInfoSection(track, wp);
    });
  };

  Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More options");
  Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
  overflowBtn->setMenu(overflowMenu);

  overflowMenu->addItem("Add waypoints before", [=](){
    if(track != activeTrack)
      populateTrack(track);
    insertionWpt = uid;
  });

  overflowMenu->addItem("Add waypoints after", [=](){
    if(track != activeTrack)
      populateTrack(track);
    auto it = track->findWaypoint(uid);
    if(it != track->waypoints.end()) ++it;
    insertionWpt = it != track->waypoints.end() ? it->uid : "";
  });

  toolRow->addWidget(chooseListBtn);
  toolRow->addWidget(createStretch());
  toolRow->addWidget(removeBtn);
  toolRow->addWidget(addNoteBtn);
  toolRow->addWidget(overflowBtn);

  section->addWidget(createHRule(2, "0 6"));
  section->addWidget(toolRow);
  section->addWidget(noteText);

  app->infoContent->selectFirst(".waypt-section")->addWidget(section);  //return section;
}

Waypoint* MapsTracks::addWaypoint(Waypoint wpt)
{
  // prevent insertion of duplicate waypoint
  if(!activeTrack->waypoints.empty()) {
    auto it0 = insertionWpt.empty() ? activeTrack->waypoints.end() : activeTrack->findWaypoint(insertionWpt);
    if((--it0)->lngLat() == wpt.lngLat())
      return NULL;
  }

  auto it = activeTrack->addWaypoint(wpt, insertionWpt);
  std::string uid = it->uid;
  bool istrack = activeTrack->routes.empty() && !activeTrack->tracks.empty();
  // we want to show all points in measure mode
  if(it->name.empty() && ((activeTrack == &navRoute && navRoute.routeMode == "direct") || istrack))
    it->name = fstring("Waypoint %d", activeTrack->wayPtSerial);
  if(it->loc.alt == 0) {
    app->getElevation(it->lngLat(), [=](double ele){
      if(!activeTrack) return;
      auto it1 = activeTrack->findWaypoint(uid);
      if(it1 != activeTrack->waypoints.end()) {
        it1->loc.alt = ele;
      }
    });
  }

  if(activeTrack->waypoints.size() == 1 && it->name.empty() && istrack)
    it->name = "Start";
  if(showAllWaypts || !it->name.empty())
    addWaypointItem(*it, insertionWpt);
  if(replaceWaypt && !insertionWpt.empty()) {
    removeWaypoint(activeTrack, insertionWpt);
    insertionWpt.clear();
  }
  if(it->routed)
    createRoute(activeTrack);
  if(it->marker <= 0)
    updateTrackMarker(activeTrack);  // create marker for waypoint if it hasn't been yet
  wayptTabLabel->setText(fstring("%d waypoints", int(activeTrack->waypoints.size())).c_str());
  return &(*it);
}

// if other panels end up needing this, use onMapEvent(PICK_RESULT) instead
bool MapsTracks::onPickResult()
{
  if(!activeTrack || !(tapToAddWaypt || stealPickResult || replaceWaypt))
    return false;
  // make lat,lng markers nameless so they can be hidden easily
  addWaypoint({app->pickResultCoord, app->pickResultName, "", app->pickResultProps});
  while(app->panelHistory.back() != trackPanel && app->popPanel()) {}
  return true;
}

bool MapsTracks::tapEvent(LngLat location)
{
  if(!activeTrack || !tapToAddWaypt || stealPickResult || replaceWaypt)
    return false;
  addWaypoint({location, ""});
  return true;
}

void MapsTracks::fingerEvent(int action, LngLat pos)
{
  if(!activeTrack) return;
  if(action < -1) {
    if(!activeTrack->routes.empty())
      activeTrack->routes.pop_back();
  }
  else {
    if(action > 0 || activeTrack->routes.empty())
      activeTrack->routes.emplace_back();
    activeTrack->routes.back().pts.push_back(Waypoint(pos));
    // request elevation for point
    size_t rteidx = activeTrack->routes.size() - 1;
    size_t ptidx = activeTrack->routes.back().pts.size()-1;
    app->getElevation(pos, [this, rteidx, ptidx, track = activeTrack](double ele){
      if(activeTrack != track || track->routes.size() <= rteidx || track->routes[rteidx].pts.size() <= ptidx) return;
      track->routes[rteidx].pts[ptidx].loc.alt = ele;
      updateStats(activeTrack);
    });
  }
  if(!activeTrack->routes.empty() && !activeTrack->routes.back().pts.empty())
    updateStats(activeTrack);
  //updateDistances();
  updateTrackMarker(activeTrack);
}

void MapsTracks::addWaypointItem(Waypoint& wp, const std::string& nextuid)
{
  std::string uid = wp.uid;
  std::string wpname = wp.name.empty() ? lngLatToStr(wp.lngLat()) : wp.name;
  const char* desc = wp.desc.empty() ? " " : wp.desc.c_str();  // make sure detail text is non-empty
  Button* item = createListItem(MapsApp::uiIcon("waypoint"), wpname.c_str(), desc);
  Widget* container = item->selectFirst(".child-container");

  item->onClicked = [this, uid](){
    if(activeTrack == &navRoute && navRoute.routeMode != "direct") {
      replaceWaypt = true;
      insertionWpt = uid;
      app->showPanel(app->mapsSearch->searchPanel, true);
    }
    else {
      auto it = activeTrack->findWaypoint(uid);
      bool wasTapToAdd = std::exchange(tapToAddWaypt, false);
      app->setPickResult(it->lngLat(), it->name, it->props);
      tapToAddWaypt = wasTapToAdd;
      setPlaceInfoSection(activeTrack, *it);
    }
  };

  if(!activeTrack->routes.empty() || activeTrack->tracks.empty()) {
    Button* routeBtn = createToolbutton(MapsApp::uiIcon("track"), "Route");
    container->addWidget(routeBtn);
    routeBtn->setChecked(wp.routed);
    routeBtn->onClicked = [=](){
      auto it = activeTrack->findWaypoint(uid);
      it->routed = !it->routed;
      routeBtn->setChecked(it->routed);
      createRoute(activeTrack);
    };
  }

  Button* editBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit");
  editBtn->onClicked = [=](){
    auto it = activeTrack->findWaypoint(uid);
    editWaypoint(activeTrack, *it, [this](){ populateTrack(activeTrack); });
  };
  container->addWidget(editBtn);

  Button* discardBtn = createToolbutton(MapsApp::uiIcon("discard"), "Remove");
  discardBtn->onClicked = [=](){ removeWaypoint(activeTrack, uid); };
  container->addWidget(discardBtn);

  wayptContent->addItem(uid, item, nextuid);

  wayptContent->onReorder = [=](std::string key, std::string next){
    auto itsrc = activeTrack->findWaypoint(key);
    auto itnext = next.empty() ? activeTrack->waypoints.end() : activeTrack->findWaypoint(next);
    bool routed = itsrc->routed;
    if(itsrc < itnext)  // moved down
      std::rotate(itsrc, itsrc+1, itnext);
    else  // moved up
      std::rotate(itnext, itsrc, itsrc+1);
    if(routed)
      createRoute(activeTrack);
  };
}

bool MapsTracks::findPickedWaypoint(GpxFile* track)
{
  for(auto& wpt : track->waypoints) {
    if(app->pickedMarkerId == wpt.marker) {
      bool wasTapToAdd = std::exchange(tapToAddWaypt, false);
      app->setPickResult(wpt.lngLat(), wpt.name, wpt.props);
      tapToAddWaypt = wasTapToAdd;
      setPlaceInfoSection(track, wpt);
      app->pickedMarkerId = 0;
      return true;
    }
  }
  return false;
}

void MapsTracks::onMapEvent(MapEvent_t event)
{
  if(event == MAP_CHANGE) {
    if(!activeTrack) return;
    // update polyline marker in direct mode
    if(directRoutePreview && activeTrack->routeMode == "direct" && !activeTrack->waypoints.empty()) {
      auto it = insertionWpt.empty() ? activeTrack->waypoints.end() : activeTrack->findWaypoint(insertionWpt);
      LngLat mappos = (--it)->lngLat();
      Point scrpos, mapcenter(app->map->getViewportWidth()/2, app->map->getViewportHeight()/2);
      app->map->lngLatToScreenPosition(mappos.longitude, mappos.latitude, &scrpos.x, &scrpos.y);
      app->crossHair->routePreviewOrigin = (scrpos - mapcenter)/MapsApp::gui->paintScale;
      double distkm = lngLatDist(mappos, app->getMapCenter());
      std::string pvstr = MapsApp::distKmToStr(distkm);
      double bearing = 180*lngLatBearing(mappos, app->getMapCenter())/M_PI;
      pvstr += fstring(" | %.1f\u00B0", bearing < 0 ? bearing + 360 : bearing);
      if(it != activeTrack->waypoints.begin() && distkm > 0) {
        LngLat prevpt = (--it)->lngLat();
        double bchange = bearing - 180*lngLatBearing(prevpt, mappos)/M_PI;
        if(bchange > 180) bchange -= 360;
        else if(bchange < -180) bchange += 360;
        pvstr += fstring(" (%+.1f\u00B0)", bchange);
      }
      previewDistText->setText(pvstr.c_str());
    }
  }
  else if(event == LOC_UPDATE) {
    updateLocation(app->currLocation);
  }
  else if(event == MARKER_PICKED) {
    if(app->pickedMarkerId <= 0) return;
    if(app->pickedMarkerId == trackHoverMarker) {
      app->setPickResult(trackHoverLoc.lngLat(), "", "");  //activeTrack->title + " waypoint"
      app->pickedMarkerId = 0;
    }
    else if(activeTrack && activeTrack->marker && activeTrack->marker->onPicked(app->pickedMarkerId)) {
      if(trackPanel->isVisible()) {
        // find the track point closest to chosen position
        double mindist = DBL_MAX;
        const Waypoint* closestWpt = NULL;
        for(const Waypoint& pt : activeTrack->activeWay()->pts) {
          double dist = lngLatDist(pt.lngLat(), app->pickResultCoord);
          if(dist > mindist) continue;
          mindist = dist;
          closestWpt = &pt;
        }
        if(closestWpt)
          trackSliders->updateValue(trackPlot->plotVsDist ? closestWpt->dist/trackPlot->maxDist
              : closestWpt->loc.time - trackPlot->minTime/(trackPlot->maxTime - trackPlot->minTime));
      }
      app->pickedMarkerId = 0;
    }
    else {
      if(!activeTrack || !findPickedWaypoint(activeTrack)) {  // navRoute is not in tracks
        for(GpxFile& track : tracks) {
          if(!track.visible || &track == activeTrack) continue;
          if(findPickedWaypoint(&track)) break;
          if(track.marker->onPicked(app->pickedMarkerId)) {
            populateTrack(&track);
            break;
          }
        }
      }
    }
  }
  else if(event == SUSPEND) {
    std::vector<std::string> order = tracksContent->getOrder();
    if(order.empty()) return;
    YAML::Node ordercfg = app->config["tracks"]["list_order"] = YAML::Node(YAML::NodeType::Sequence);
    for(const std::string& s : order)
      ordercfg.push_back(s);
    if(activeTrack && activeTrack->modified)
      activeTrack->modified = !saveGPX(activeTrack);
  }
}

void MapsTracks::setRouteMode(const std::string& mode)
{
  auto parts = splitStr<std::vector>(mode.c_str(), '-');
  const char* icon = "segment";
  if(parts.empty()) {}
  else if(parts[0] == "draw") icon = "scribble";
  else if(parts[0] == "walk") icon = "walk";
  else if(parts[0] == "bike") icon = "bike";
  else if(parts[0] == "drive") icon = "car";
  routeModeBtn->setIcon(MapsApp::uiIcon(icon));
  //previewRoute.clear();
  if(directRoutePreview && mode == "direct")
    onMapEvent(MAP_CHANGE);
  else
    app->crossHair->routePreviewOrigin = {NAN, NAN};  //app->map->markerSetVisible(previewMarker, false);
  app->drawOnMap = routeEditTb->isVisible() && mode == "draw";
  if(!activeTrack || activeTrack->routeMode == mode) return;
  activeTrack->routeMode = mode;
  createRoute(activeTrack);
}

void MapsTracks::toggleRouteEdit(bool show)
{
  routeEditBtn->setChecked(show);
  routeEditTb->setVisible(show);
  app->crossHair->setVisible(show);
  directRoutePreview = show;
  app->drawOnMap = show && activeTrack->routeMode == "draw";
  //setRouteMode(activeTrack->routeMode);  // update previewMarker
}

void MapsTracks::addPlaceActions(Toolbar* tb)
{
  if(activeTrack) {
    Button* addWptBtn = createToolbutton(MapsApp::uiIcon("waypoint"), "Add waypoint");
    addWptBtn->onClicked = [=](){
      std::string nm = app->currLocPlaceInfo ? "Location at " + ftimestr("%H:%M:%S %F") : app->pickResultName;
      Waypoint* wpt = addWaypoint({app->pickResultCoord, nm, "", app->pickResultProps});
      if(wpt)
        setPlaceInfoSection(activeTrack, *wpt);
    };
    tb->addWidget(addWptBtn);
  }
  else {
    Button* routeBtn = createToolbutton(MapsApp::uiIcon("directions"), "Directions");
    routeBtn->onClicked = [=](){
      LngLat r1 = app->currLocation.lngLat(), r2 = app->pickResultCoord;
      double km = lngLatDist(r1, r2);
      navRoute = GpxFile();  //removeTrackMarkers(&navRoute);
      navRoute.title = "Navigation";
      navRoute.routeMode = km < 10 ? "walk" : km < 100 ? "bike" : "drive";
      if(km > 0.01)
        navRoute.addWaypoint({r1, "Start"});  //"Current location"
      navRoute.addWaypoint({r2, app->pickResultName, "", app->pickResultProps});
      activeTrack = NULL;
      createRoute(&navRoute);
      app->showPanel(tracksPanel);
      app->panelToSkip = tracksPanel;
      populateTrack(&navRoute);
      toggleRouteEdit(false);
    };
    tb->addWidget(routeBtn);

    Button* measureBtn = createToolbutton(MapsApp::uiIcon("measure"), "Measure");
    measureBtn->onClicked = [=](){
      navRoute = GpxFile();  //removeTrackMarkers(&navRoute);
      navRoute.title = "Measurement";
      navRoute.routeMode = "direct";
      navRoute.addWaypoint({app->pickResultCoord, app->pickResultName, "", app->pickResultProps});
      activeTrack = NULL;
      createRoute(&navRoute);
      app->showPanel(tracksPanel);
      app->panelToSkip = tracksPanel;
      populateTrack(&navRoute);
      toggleRouteEdit(true);
    };
    tb->addWidget(measureBtn);
  }
}

static Widget* createStatsRow(std::vector<const char*> items)  // const char* title1, const char* class1, const char* title2, const char* class2)
{
  static const char* statBlockProtoSVG = R"(
    <g layout="box">
      <rect fill="none" width="150" height="40"/>
      <text class="title-text weak" box-anchor="left top" margin="0 0 0 10" font-size="12"></text>
      <text class="stat-text" box-anchor="left top" margin="15 0 0 16">
        <tspan class="stat-value-tspan" font-size="16"></tspan>
        <tspan class="stat-units-tspan" font-size="13"></tspan>
      </text>
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

void MapsTracks::updateDB(GpxFile* track)
{
  if(track->filename.empty()) {
    FSPath file(MapsApp::baseDir, "tracks/" + track->title + ".gpx");
    std::string basepath = file.basePath();
    for(int ii = 1; file.exists(); ++ii)
      file = basepath + fstring(" (%d).gpx", ii);
    track->filename = file.path;
  }
  if(track->rowid < 0) {
    SQLiteStmt(app->bkmkDB, "INSERT INTO tracks (title, style, filename) VALUES (?,?,?);")
        .bind(track->title, track->style, track->filename).exec();
    track->rowid = sqlite3_last_insert_rowid(app->bkmkDB);
  }
  else
    SQLiteStmt(app->bkmkDB, "UPDATE tracks SET title = ?, style = ?, filename = ? WHERE rowid = ?;")
        .bind(track->title, track->style, track->filename, track->rowid).exec();
  track->loaded = true;
  tracksDirty = true;
}

void MapsTracks::setTrackEdit(bool show)
{
  if(show == editTrackTb->isVisible()) return;
  if(show && (!plotWidgets[0]->isVisible() || recordTrack)) return;
  editTrackTb->setVisible(show);
  trackSliders->setEditMode(show);
  app->map->markerSetVisible(trackHoverMarker, !show);
  if(show)
    trackSliders->setCropHandles(0, 1, TrackSliders::FORCE_UPDATE);
  else {
    app->map->markerSetVisible(trackStartMarker, false);
    app->map->markerSetVisible(trackEndMarker, false);
    if(!origLocs.empty()) {
      activeTrack->activeWay()->pts = std::move(origLocs);
      updateTrackMarker(activeTrack);  // rebuild marker
      //populateStats(activeTrack);
      trackPlot->zoomScale = 1.0;
      updateStats(activeTrack);
    }
  }
}

// currently, we need to create separate widgets for stats and waypoints panels
Widget* MapsTracks::createEditDialog(Button* editTrackBtn)
{
  TextEdit* editTrackTitle = createTitledTextEdit("Title");
  ColorPicker* editTrackColor = createColorPicker(app->colorPickerMenu, Color::BLUE);
  editTrackColor->node->setAttribute("box-anchor", "bottom");
  //CheckBox* editTrackCopyCb = createCheckBox("Save as copy", false);
  //editTrackCopyCb->node->setAttribute("box-anchor", "left");
  Widget* editTrackRow = createRow();
  editTrackRow->addWidget(editTrackTitle);
  editTrackRow->addWidget(editTrackColor);
  auto saveTrackFn = [=](bool savecopy){
    auto title = editTrackTitle->text();
    bool sametitle = title == activeTrack->title;
    // add entry for original track if making copy
    if(activeTrack->rowid >= 0 && savecopy) {
      tracks.emplace_back(activeTrack->title, activeTrack->desc, activeTrack->filename,
          activeTrack->style, activeTrack->rowid, activeTrack->archived);
    }
    activeTrack->title = title;
    activeTrack->style = colorToStr(editTrackColor->color());
    //if(activeTrack->marker)
    updateTrackMarker(activeTrack);  // apply (potentially) new color to markers
    tracksDirty = true;
    Widget* titleWidget = app->panelHistory.back()->selectFirst(".panel-title");
    if(titleWidget)
      static_cast<TextLabel*>(titleWidget)->setText(activeTrack->title.c_str());
    if(activeTrack->rowid >= 0 || activeTrack == &recordedTrack) {
      if(savecopy) {
        activeTrack->rowid = -1;
        activeTrack->filename.clear();
        if(sametitle)
          activeTrack->title.append(" Copy");
      }
      updateDB(activeTrack);
      saveGPX(activeTrack);
      origLocs.clear();
    }
    setTrackEdit(true);
  };
  Widget* saveTrackContent = createInlineDialog(
      {editTrackRow}, "Save Copy", [=](){ saveTrackFn(true); }, [=](){ setTrackEdit(false); });

  Button* saveCopyBtn = static_cast<Button*>(saveTrackContent->selectFirst(".accept-btn"));
  saveCopyBtn->setIcon(NULL);
  Button* saveBtn = createToolbutton(uiIcon("save"), "Save", true);
  saveBtn->onClicked = [=](){ saveTrackFn(false); };
  saveCopyBtn->parent()->addWidget(saveBtn);

  editTrackTitle->onChanged = [=](const char* s){ saveCopyBtn->setEnabled(s[0]); };
  saveTrackContent->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      editTrackTitle->setText(activeTrack->title.c_str());
      editTrackColor->setColor(parseColor(activeTrack->style, Color::BLUE));
      //editTrackCopyCb->setChecked(false);
      saveCopyBtn->setVisible(activeTrack->rowid >= 0);
    }
    return false;
  });
  return saveTrackContent;
}

void MapsTracks::createStatsContent()
{
  // Stats panel
  statsContent = createColumn();

  // bearing? direction (of travel)?
  //statsContent->addWidget(createStatsRow({"Latitude", "track-latitude", "Longitude", "track-longitude"}));
  //Widget* statsCol = createColumn({
  //    createStatsRow({"Altitude", "track-altitude", "Speed", "track-speed"}),
  //    createStatsRow({"Total time", "track-time", "Moving time", "track-moving-time"}),
  //    createStatsRow({"Distance", "track-dist", "Moving speed", "track-avg-speed"}),
  //    createStatsRow({"Ascent", "track-ascent", "Descent", "track-descent"}),
  //    createStatsRow({"Ascent speed", "track-ascent-speed", "Descent speed", "track-descent-speed"}) });

  liveStatsRow = createStatsRow({"Altitude", "track-altitude", "Speed", "track-speed",
      "Slope", "track-slope", "Direction", "track-direction"});
  nonliveStatsRow = createStatsRow({"Start", "track-start-date", "End", "track-end-date"});
  Widget* statsCol = createColumn({ liveStatsRow, nonliveStatsRow,
      createStatsRow({"Total time", "track-time", "Moving time", "track-moving-time",
                      "Distance", "track-dist", "Moving speed", "track-avg-speed"}),
      createStatsRow({"Ascent", "track-ascent", "Ascent speed", "track-ascent-speed",
                      "Descent", "track-descent", "Descent speed", "track-descent-speed"}) });

  statsContent->addWidget(statsCol);
  trackContainer->addWidget(statsContent);
  statsWidgets.push_back(statsContent);
}

void MapsTracks::createPlotContent()
{
  Button* vertAxisSelBtn = createToolbutton(uiIcon("chevron-down"), "Altitude", true);
  vertAxisSelBtn->node->selectFirst(".toolbutton-content")->setAttribute("flex-direction", "row-reverse");
  Menu* vertAxisMenu = createMenu(Menu::VERT_LEFT);
  vertAxisSelBtn->setMenu(vertAxisMenu);
  Button* plotAltiBtn = createCheckBoxMenuItem("Altitude");
  Button* plotSpeedBtn = createCheckBoxMenuItem("Speed");
  vertAxisMenu->addItem(plotAltiBtn);
  vertAxisMenu->addItem(plotSpeedBtn);
  plotAltiBtn->setChecked(true);

  Button* horzAxisSelBtn = createToolbutton(uiIcon("chevron-down"), "Distance", true);
  horzAxisSelBtn->node->selectFirst(".toolbutton-content")->setAttribute("flex-direction", "row-reverse");
  Button* plotVsDistBtn = createCheckBoxMenuItem("Distance", "#radiobutton");
  plotVsTimeBtn = createCheckBoxMenuItem("Time", "#radiobutton");
  Menu* horzAxisMenu = createMenu(Menu::VERT_LEFT);
  horzAxisSelBtn->setMenu(horzAxisMenu);
  horzAxisMenu->addItem(plotVsDistBtn);
  horzAxisMenu->addItem(plotVsTimeBtn);
  plotVsDistBtn->setChecked(true);

  auto updatePlotAxisBtns = [=](){
    std::string altstr = MapsApp::metricUnits ? "Altitude (m)" : "Altitude (ft)";  //"Altitude"
    std::string spdstr = MapsApp::metricUnits ? "Speed (km/h)" : "Speed (mph)";  //"Speed"
    vertAxisSelBtn->setTitle((trackPlot->plotAlt && trackPlot->plotSpd ?
        (altstr + ", " + spdstr) : (trackPlot->plotAlt ? altstr : spdstr)).c_str());
    horzAxisSelBtn->setTitle(trackPlot->plotVsDist ?
        (MapsApp::metricUnits ? "Distance (km)" : "Distance (mi)") : "Time");  //"Distance"
  };

  plotAltiBtn->onClicked = [=](){
    plotAltiBtn->setChecked(!plotAltiBtn->isChecked());
    trackPlot->plotAlt = plotAltiBtn->isChecked();
    if(!trackPlot->plotAlt && !trackPlot->plotSpd) {
      trackPlot->plotSpd = true;
      plotSpeedBtn->setChecked(true);
    }
    updatePlotAxisBtns();
    trackPlot->redraw();
  };

  plotSpeedBtn->onClicked = [=](){
    plotSpeedBtn->setChecked(!plotSpeedBtn->isChecked());
    trackPlot->plotSpd = plotSpeedBtn->isChecked();
    if(!trackPlot->plotAlt && !trackPlot->plotSpd) {
      trackPlot->plotAlt = true;
      plotAltiBtn->setChecked(true);
    }
    updatePlotAxisBtns();
    trackPlot->redraw();
  };

  auto horzAxisSelFn = [=](bool vsdist){
    trackPlot->plotVsDist = vsdist;
    plotVsDistBtn->setChecked(vsdist);
    plotVsTimeBtn->setChecked(!vsdist);
    updatePlotAxisBtns();
    trackPlot->redraw();
  };
  plotVsTimeBtn->onClicked = [=](){ horzAxisSelFn(false); };
  plotVsDistBtn->onClicked = [=](){ horzAxisSelFn(true); };

  Toolbar* axisSelRow = createToolbar({vertAxisSelBtn, new TextBox(createTextNode("vs.")), horzAxisSelBtn});

  trackPlot = new TrackPlot();
  trackPlot->node->setAttribute("box-anchor", "hfill");

  trackSliders = createTrackSliders();

  trackSliders->onStartHandleChanged = [=](){
    cropStart = trackPlot->plotPosToTrackPos(trackSliders->startHandlePos);
    Waypoint startloc = interpTrack(activeTrack->activeWay()->pts, cropStart);
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
    Waypoint endloc = interpTrack(activeTrack->activeWay()->pts, cropEnd);
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

  Button* cropTrackBtn = createToolbutton(NULL, "Crop to segment", true);
  cropTrackBtn->onClicked = [=](){
    if(origLocs.empty()) origLocs = activeTrack->activeWay()->pts;
    auto& locs = activeTrack->activeWay()->pts;
    std::vector<Waypoint> newlocs;
    size_t startidx, endidx;
    newlocs.push_back(interpTrack(locs, cropStart, &startidx));
    auto endloc = interpTrack(locs, cropEnd, &endidx);
    newlocs.insert(newlocs.end(), locs.begin() + startidx, locs.begin() + endidx);
    newlocs.push_back(endloc);
    activeTrack->activeWay()->pts.swap(newlocs);
    trackSliders->setCropHandles(0, 1, TrackSliders::FORCE_UPDATE);
    updateTrackMarker(activeTrack);  // rebuild marker
    //populateStats(activeTrack);
    trackPlot->zoomScale = 1.0;
    updateStats(activeTrack);
  };

  Button* deleteSegmentBtn = createToolbutton(NULL, "Delete Segment", true);
  deleteSegmentBtn->onClicked = [=](){
    if(origLocs.empty()) origLocs = activeTrack->activeWay()->pts;
    auto& locs = activeTrack->activeWay()->pts;
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
    //populateStats(activeTrack);
    trackPlot->zoomScale = 1.0;
    updateStats(activeTrack);
  };

  Button* moreTrackOptionsBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More options");
  Menu* trackPlotOverflow = createMenu(Menu::VERT_LEFT);
  moreTrackOptionsBtn->setMenu(trackPlotOverflow);

  trackPlotOverflow->addItem("Append track", [this](){
    if(!selectTrackDialog) {
      selectTrackDialog.reset(createSelectDialog("Choose Track", MapsApp::uiIcon("track")));
      selectTrackDialog->onSelected = [this](int idx){
        auto order = tracksContent->getOrder();
        int rowid = std::stoi(order[idx]);
        auto it = std::find_if(tracks.rbegin(), tracks.rend(), [=](const GpxFile& a){ return a.rowid == rowid; });
        GpxWay* way = it != tracks.rend() ? it->activeWay() : NULL;
        if(!way) return;
        if(origLocs.empty()) origLocs = activeTrack->activeWay()->pts;
        auto& locs = activeTrack->activeWay()->pts;
        locs.insert(locs.end(), way->pts.begin(), way->pts.end());
        updateTrackMarker(activeTrack);  // rebuild marker
        //populateStats(activeTrack);
        trackPlot->zoomScale = 1.0;
        updateStats(activeTrack);
      };
    }

    auto order = tracksContent->getOrder();
    std::vector<std::string> items;
    items.reserve(order.size());
    for(const std::string& idstr : order) {
      int rowid = std::stoi(idstr);
      auto it = std::find_if(tracks.rbegin(), tracks.rend(), [=](const GpxFile& a){ return a.rowid == rowid; });
      if(it == tracks.rend()) continue;  // shouldn't happen
      items.push_back(it->title);
    }
    selectTrackDialog->addItems(items);
    showModalCentered(selectTrackDialog.get(), MapsApp::gui);
  });

  trackPlotOverflow->addItem("Reverse Track", [this](){
    if(origLocs.empty()) origLocs = activeTrack->activeWay()->pts;
    auto& locs = activeTrack->activeWay()->pts;
    std::reverse(locs.begin(), locs.end());
    updateTrackMarker(activeTrack);  // rebuild marker
    //populateStats(activeTrack);
    trackPlot->zoomScale = 1.0;
    updateStats(activeTrack);
  });

  editTrackTb = createToolbar({cropTrackBtn, deleteSegmentBtn, createStretch(), moreTrackOptionsBtn});
  editTrackTb->setVisible(false);

  trackSliders->onValueChanged = [=](real s){
    if(editTrackTb->isVisible()) return;  // disabled when editing
    if(s < 0 || s > 1 || !activeTrack) {
      app->map->markerSetVisible(trackHoverMarker, false);
      return;
    }
    real pos = trackPlot->plotPosToTrackPos(s);
    trackHoverLoc = interpTrack(activeTrack->activeWay()->pts, pos);
    if(trackHoverMarker == 0) {
      trackHoverMarker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(trackHoverMarker, "layers.track-marker.draw.marker");
    }
    app->map->markerSetVisible(trackHoverMarker, true);
    app->map->markerSetPoint(trackHoverMarker, trackHoverLoc.lngLat());
    // or fixed color? or yellow?
    app->map->markerSetProperties(trackHoverMarker, {{{"color", activeTrack->style}}});
  };

  trackPlot->onPanZoom = [=](){
    real start = trackPlot->trackPosToPlotPos(cropStart);
    real end = trackPlot->trackPosToPlotPos(cropEnd);
    trackSliders->setCropHandles(start, end, TrackSliders::NO_UPDATE);
  };

  auto plotContainer = createColumn({axisSelRow, trackPlot, trackSliders, editTrackTb}, "", "", "hfill");
  trackContainer->addWidget(plotContainer);
  plotWidgets.push_back(plotContainer);

  plotContainer->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::INVISIBLE) {
      app->map->markerSetVisible(trackHoverMarker, false);
    }
    return false;
  });
}

void MapsTracks::createWayptContent()
{
  wayptContent = new DragDropList;

  saveRouteBtn = createToolbutton(MapsApp::uiIcon("save"), "Save");
  saveRouteBtn->onClicked = [=](){
    if(activeTrack == &navRoute) {
      auto& dest = navRoute.waypoints.back().name;
      tracks.push_back(std::move(navRoute));
      tracks.back().title = dest.empty() ? "Route" : "Route to " + dest;
      tracks.back().desc = ftimestr("%F");
      tracks.back().filename = "";  //saveRouteFile->text();
      tracks.back().visible = true;
      updateDB(&tracks.back());
      populateTrack(&tracks.back());
      //saveRouteTitle->setText("");
      //saveRouteFile->setText("");
      //showInlineDialogModal(saveRouteContent);
    }
    else if(activeTrack->filename.empty())
      LOGE("Active track has no filename!");
    else
      activeTrack->modified = !saveGPX(activeTrack);
  };

  bool hasPlugins = !app->pluginManager->routeFns.empty();
  routePluginBtn = createToolbutton(MapsApp::uiIcon(hasPlugins ? "plugin" : "no-plugin"), "Plugin");
  routePluginBtn->setEnabled(hasPlugins);
  if(hasPlugins) {
    std::vector<std::string> pluginTitles;
    for(auto& fn : app->pluginManager->routeFns)
      pluginTitles.push_back(fn.title.c_str());
    Menu* routePluginMenu = createRadioMenu(pluginTitles, [=](size_t idx){
      pluginFn = idx;
      if(activeTrack && activeTrack->routeMode != "direct")
        createRoute(activeTrack);
    });
    routePluginBtn->setMenu(routePluginMenu);
  }

  routeModeBtn = createToolbutton(MapsApp::uiIcon("segment"), "Routing");
  Menu* routeModeMenu = createMenu(Menu::VERT_LEFT);
  routeModeMenu->addItem("Direct", MapsApp::uiIcon("segment"), [=](){ setRouteMode("direct"); });
  routeModeMenu->addItem("Draw", MapsApp::uiIcon("scribble"), [=](){ setRouteMode("draw"); });
  routeModeMenu->addItem("Walk", MapsApp::uiIcon("walk"), [=](){ setRouteMode("walk"); });
  routeModeMenu->addItem("Cycle", MapsApp::uiIcon("bike"), [=](){ setRouteMode("bike"); });
  routeModeMenu->addItem("Drive", MapsApp::uiIcon("car"), [=](){ setRouteMode("drive"); });
  routeModeBtn->setMenu(routeModeMenu);
  routeModeBtn->setEnabled(hasPlugins);

  Button* showAllWptsBtn = createCheckBoxMenuItem("Show untitled waypoints");
  showAllWptsBtn->onClicked = [=](){
    showAllWaypts = !showAllWaypts;
    showAllWptsBtn->setChecked(showAllWaypts);
    populateTrack(activeTrack);
  };
  showAllWptsBtn->setChecked(showAllWaypts);
  trackOverflow->addItem(showAllWptsBtn);

  // route edit toolbar - I think this will eventually be a floating toolbar
  previewDistText = new TextBox(createTextNode(""));
  previewDistText->setMargins(6, 10);

  retryBtn = createToolbutton(MapsApp::uiIcon("retry"), "Retry");
  retryBtn->onClicked = [=](){ createRoute(activeTrack); };
  retryBtn->setVisible(false);

  Button* mapCenterWayptBtn = createToolbutton(MapsApp::uiIcon("crosshair"), "Add map center");
  mapCenterWayptBtn->onClicked = [=](){ addWaypoint({app->getMapCenter(), ""}); };

  Button* searchWayptBtn = createToolbutton(MapsApp::uiIcon("search"), "Add from search");
  searchWayptBtn->onClicked = [=](){ stealPickResult = true; app->showPanel(app->mapsSearch->searchPanel, true); };

  Button* bkmkWayptBtn = createToolbutton(MapsApp::uiIcon("pin"), "Add from bookmarks");
  bkmkWayptBtn->onClicked = [=](){ stealPickResult = true; app->showPanel(app->mapsBookmarks->listsPanel, true); };

  Button* rteEditOverflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More options");
  Menu* rteEditOverflow = createMenu(Menu::VERT_LEFT);
  rteEditOverflowBtn->setMenu(rteEditOverflow);

  rteEditOverflow->addItem("Clear route", [this](){
    if(activeTrack && !activeTrack->routes.empty())
      activeTrack->routes.pop_back();
    if(!activeTrack->routes.empty())
      updateStats(activeTrack);
    updateTrackMarker(activeTrack);
  });

  Button* tapToAddWayptBtn = createCheckBoxMenuItem("Tap to add waypoint");
  tapToAddWayptBtn->onClicked = [=](){
    tapToAddWaypt = !tapToAddWaypt;
    tapToAddWayptBtn->setChecked(tapToAddWaypt);
  };
  tapToAddWayptBtn->setChecked(tapToAddWaypt);
  rteEditOverflow->addItem(tapToAddWayptBtn);

  routeEditTb = createToolbar({ previewDistText, createStretch(),
      retryBtn, mapCenterWayptBtn, searchWayptBtn, bkmkWayptBtn, rteEditOverflowBtn });

  auto wayptContainer = createColumn({ routeEditTb, sparkStats, wayptContent }, "", "", "hfill");
  trackContainer->addWidget(wayptContainer);
  wayptWidgets.push_back(wayptContainer);

  routeEditBtn = createToolbutton(MapsApp::uiIcon("draw-track"), "Edit Route");
  routeEditBtn->onClicked = [=](){
    toggleRouteEdit(!routeEditBtn->isChecked());
    setRouteMode(activeTrack->routeMode);  // update previewMarker
  };
  routeEditTb->setVisible(false);

  auto wayptsTb = createRow({saveRouteBtn, routeEditBtn, routeModeBtn, routePluginBtn}, "", "", "");
  trackToolbar->addWidget(wayptsTb);
  wayptWidgets.push_back(wayptsTb);

  wayptContainer->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      replaceWaypt = false;  // clear this when coming back from search, regardless of point choosen or not
      stealPickResult = false;
      directRoutePreview = routeEditTb->isVisible();
      app->crossHair->setVisible(directRoutePreview);
      if(waypointsDirty)
        populateWaypoints(activeTrack);
    }
    else if(event->type == SvgGui::INVISIBLE) {
      directRoutePreview = false;
      app->drawOnMap = false;
      app->crossHair->setVisible(false);
      app->crossHair->routePreviewOrigin = {NAN, NAN};  //app->map->markerSetVisible(previewMarker, false);
    }
    return false;
  });
}

void MapsTracks::createTrackPanel()
{
  trackToolbar = app->createPanelHeader(MapsApp::uiIcon("track"), "");
  trackOverflow = createMenu(Menu::VERT_LEFT);
  trackContainer = createColumn({}, "", "", "fill");

  editTrackContent = createEditDialog(NULL);
  trackContainer->addWidget(editTrackContent);

  pauseRecordBtn = createToolbutton(MapsApp::uiIcon("pause"), "Pause");
  stopRecordBtn = createToolbutton(MapsApp::uiIcon("stop"), "Stop");

  Widget* stopPauseBtns = createRow({pauseRecordBtn, stopRecordBtn}, "", "", "");
  trackToolbar->addWidget(stopPauseBtns);
  statsWidgets.push_back(stopPauseBtns);
  plotWidgets.push_back(stopPauseBtns);

  pauseRecordBtn->onClicked = [=](){
    recordTrack = !recordTrack;
    recordedTrack.desc = (recordTrack ? "Recording" : "Paused") + trackSummary;
    tracksDirty = true;
    pauseRecordBtn->setChecked(!recordTrack);  // should actually toggle between play and pause icons
    // show/hide track editing controls
    if(recordTrack && editTrackTb->isVisible())
      setTrackEdit(false);
  };

  stopRecordBtn->onClicked = [=](){
    saveGPX(&recordedTrack);
    //updateDB(&recordedTrack);
    recordedTrack.desc = ftimestr("%F") + trackSummary;
    tracks.push_back(std::move(recordedTrack));
    recordedTrack = GpxFile();
    recordTrack = false;
    tracksDirty = true;
    pauseRecordBtn->setChecked(false);
    populateTrack(&tracks.back());
  };

  saveCurrLocBtn = trackOverflow->addItem("Save location", [=](){
    //std::string namestr = "Location at " + ftimestr("%H:%M:%S %F");
    std::string namestr = "Location at T+"
        + durationToStr(app->currLocation.time - activeTrack->activeWay()->pts.front().loc.time);
    Waypoint* wpt = addWaypoint({app->currLocation, namestr});
    editWaypoint(activeTrack, *wpt, {});
  });

  trackPanel = app->createMapPanel(trackToolbar, trackContainer);
  trackPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE || event->type == SvgGui::INVISIBLE) {
      if(statsWidgets[0]->isVisible()) { for(Widget* w : statsWidgets) w->sdlEvent(gui, event); }
      else if(plotWidgets[0]->isVisible()) { for(Widget* w : plotWidgets) w->sdlEvent(gui, event); }
      else if(wayptWidgets[0]->isVisible()) { for(Widget* w : wayptWidgets) w->sdlEvent(gui, event); }
      if(event->type == SvgGui::INVISIBLE) {
        editTrackContent->setVisible(false);
        setTrackEdit(false);
      }
    }
    else if(event->type == MapsApp::PANEL_CLOSED) {
      app->pluginManager->cancelRequests(PluginManager::ROUTE);
      if(activeTrack && activeTrack->modified)
        activeTrack->modified = !saveGPX(activeTrack);
      if(activeTrack && app->panelHistory.back() != trackPanel) {
        if(activeTrack != &recordedTrack)
          activeTrack->marker->setStylePath("layers.track.draw.track");
        if(!activeTrack->visible)
          showTrack(activeTrack, false);
        activeTrack = NULL;
      }
    }
    return false;
  });

  // tab bar for switching between stats, plot, and waypoints
  static const char* sparkStatsSVG = R"(
    <g layout="box" box-anchor="vfill">
      <rect class="min-width-rect" width="60" height="20" fill="none"/>
      <g layout="flex" flex-direction="column" box-anchor="vfill" margin="0 2" font-size="13">
        <text class="spark-dist" box-anchor="left"></text>
        <text class="spark-time" box-anchor="left"></text>
        <text display="none" class="spark-ascent" box-anchor="left"></text>
        <text display="none" class="spark-descent" box-anchor="left"></text>
      </g>
    </g>
  )";
  sparkStats = new Widget(loadSVGFragment(sparkStatsSVG));

  trackSpark = new TrackSparkline();
  trackSpark->mBounds = Rect::wh(200, 50);  //80);
  trackSpark->node->setAttribute("box-anchor", "hfill");
  trackSpark->setMargins(1, 1);

  wayptTabLabel = new TextBox(createTextNode(""));

  Button* statsTabBtn = createToolbutton(uiIcon("sigma"), "Statistics");
  statsTabBtn->selectFirst(".toolbutton-content")->addWidget(sparkStats);
  statsWidgets.push_back(statsTabBtn->selectFirst(".checkmark"));
  statsWidgets.back()->node->addClass("checked");
  statsTabBtn->onClicked = [=](){ setTrackWidgets(TRACK_STATS); };

  Button* plotTabBtn = createToolbutton(uiIcon("graph-line"), "Plot");
  plotTabBtn->selectFirst(".toolbutton-content")->addWidget(trackSpark);
  plotWidgets.push_back(plotTabBtn->selectFirst(".checkmark"));
  plotWidgets.back()->node->addClass("checked");
  plotTabBtn->onClicked = [=](){ setTrackWidgets(TRACK_PLOT); };

  Button* wayptTabBtn = createToolbutton(uiIcon("waypoint"), "Waypoints");
  wayptTabBtn->selectFirst(".toolbutton-content")->addWidget(wayptTabLabel);
  wayptWidgets.push_back(wayptTabBtn->selectFirst(".checkmark"));
  wayptWidgets.back()->node->addClass("checked");
  wayptTabBtn->onClicked = [=](){ setTrackWidgets(TRACK_WAYPTS); };

  Widget* tabBarRow = createRow({statsTabBtn, plotTabBtn, wayptTabBtn});
  trackContainer->addWidget(tabBarRow);

  // create content for three views
  createStatsContent();
  createPlotContent();
  createWayptContent();

  // bottom of menu
  trackOverflow->addItem("Edit", [=](){ showInlineDialogModal(editTrackContent); });

  trackOverflow->addItem("Export", [=](){
    MapsApp::saveFileDialog({{PLATFORM_MOBILE ? "application/gpx+xml" : "GPX file", "gpx"}},
        activeTrack->title, [this](const char* s){ saveGPX(activeTrack, s); });
  });

  // end of toolbar
  Button* trackOverflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More options");
  trackOverflowBtn->setMenu(trackOverflow);
  trackToolbar->addWidget(trackOverflowBtn);
}

void MapsTracks::createTrackListPanel()
{
  Button* drawTrackBtn = createToolbutton(MapsApp::uiIcon("add-track"), "Create Route");
  TextEdit* newTrackTitle = createTitledTextEdit("Title");
  ColorPicker* newTrackColor = createColorPicker(app->colorPickerMenu, Color::BLUE);
  newTrackColor->node->setAttribute("box-anchor", "bottom");
  Widget* newTrackRow = createRow({newTrackTitle, newTrackColor});

  newTrackDialog.reset(createInputDialog({newTrackRow}, "New Route", "Create", [=](){
    tracks.emplace_back(newTrackTitle->text(), ftimestr("%F"), "");
    updateDB(&tracks.back());
    populateTrack(&tracks.back());
    toggleRouteEdit(true);
  }));
  newTrackTitle->onChanged = [=](const char* s){ newTrackDialog->selectFirst(".accept-btn")->setEnabled(s[0]); };

  drawTrackBtn->onClicked = [=](){
    newTrackDialog->focusedWidget = NULL;
    showModalCentered(newTrackDialog.get(), app->gui);  //showInlineDialogModal(editContent);
    newTrackTitle->setText(ftimestr("%FT%H.%M.%S").c_str());  //"%Y-%m-%d %HH%M"
    app->gui->setFocused(newTrackTitle, SvgGui::REASON_TAB);
  };

  Button* loadTrackBtn = createToolbutton(MapsApp::uiIcon("open-folder"), "Load Track");
  auto loadTrackFn = [=](const char* filename){
    GpxFile track("", "", filename);
    loadGPX(&track);
    if(track.waypoints.empty() && !track.activeWay()) {
      MapsApp::messageBox("Load track", fstring("Error reading %s", filename), {"OK"});
      return;
    }
    tracks.push_back(std::move(track));
    updateDB(&tracks.back());
    populateTrack(&tracks.back());
  };
  loadTrackBtn->onClicked = [=](){ MapsApp::openFileDialog({{"GPX files", "gpx"}}, loadTrackFn); };

  Button* recordTrackBtn = createToolbutton(MapsApp::uiIcon("record"), "Record Track");
  recordTrackBtn->onClicked = [=](){
    if(!recordedTrack.tracks.empty())
      populateTrack(&recordedTrack);  // show stats panel for recordedTrack, incl pause and stop buttons
    else {
      recordTrack = true;
      std::string timestr = ftimestr("%FT%H.%M.%S");
      FSPath gpxPath(app->baseDir, "tracks/" + timestr + ".gpx");
      recordedTrack = GpxFile(timestr, "Recording", gpxPath.path);  //Track{timestr, "", gpxPath.c_str(), "", 0, {}, -1, true, false};
      recordedTrack.loaded = true;
      recordedTrack.tracks.emplace_back();
      recordedTrack.tracks.back().pts.push_back(app->currLocation);
      recordedTrack.style = "#FF6030";  // use color in marker style instead?
      recordedTrack.marker = std::make_unique<TrackMarker>(app->map.get(), "layers.recording-track.draw.track");
      tracksDirty = true;
      setTrackVisible(&recordedTrack, true);
      populateTrack(&recordedTrack);
      setTrackWidgets(TRACK_STATS);  // plot isn't very useful until there are enough points
      editTrackContent->setVisible(true);
    }
  };

  tracksContent = new DragDropList;  //createColumn();
  auto tracksTb = app->createPanelHeader(MapsApp::uiIcon("folder"), "Tracks");
  tracksTb->addWidget(drawTrackBtn);
  tracksTb->addWidget(loadTrackBtn);
  tracksTb->addWidget(recordTrackBtn);
  tracksPanel = app->createMapPanel(tracksTb, NULL, tracksContent, false);

  tracksPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      if(tracksDirty)
        populateTrackList();
    }
    return false;
  });

  archivedContent = createColumn();
  auto archivedHeader = app->createPanelHeader(MapsApp::uiIcon("archive"), "Archived Tracks");
  archivedPanel = app->createMapPanel(archivedHeader, archivedContent, NULL, false);
}

Button* MapsTracks::createPanel()
{
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS tracks(title TEXT, filename TEXT, style TEXT,"
      " timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)), archived INTEGER DEFAULT 0);");

  createTrackListPanel();
  createTrackPanel();

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

  // main toolbar button ... quick menu - visibility toggle for recent tracks for now
  Menu* tracksMenu = createMenu(Menu::VERT);
  tracksMenu->addHandler([=](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      int uiWidth = app->getPanelWidth();
      gui->deleteContents(tracksMenu->selectFirst(".child-container"));
      if(recordTrack) {
        Button* item = createCheckBoxMenuItem("Current track");
        item->onClicked = [this](){ setTrackVisible(&recordedTrack, !recordedTrack.visible); };
        item->setChecked(recordedTrack.visible);
        tracksMenu->addItem(item);
      }
      if(tracksDirty)
        populateTrackList();  // needed to get order
      auto items = tracksContent->getOrder();
      for(size_t ii = 0; ii < (recordTrack ? 9 : 10) && ii < items.size(); ++ii) {
        int rowid = std::stoi(items[ii]);
        auto it = std::find_if(tracks.rbegin(), tracks.rend(), [=](const GpxFile& a){ return a.rowid == rowid; });
        if(it == tracks.rend()) continue;  // shouldn't happen
        GpxFile* track = &(*it);
        Button* item = createCheckBoxMenuItem(track->title.c_str());
        tracksMenu->addItem(item);
        SvgPainter::elideText(static_cast<SvgText*>(item->selectFirst(".title")->node), uiWidth - 100);
        item->onClicked = [=](){ setTrackVisible(track, !track->visible); };
        item->setChecked(track->visible || track == activeTrack);
        break;
      }
    }
    return false;
  });

  Button* tracksBtn = app->createPanelButton(MapsApp::uiIcon("track"), "Tracks", tracksPanel);
  tracksBtn->setMenu(tracksMenu);
  return tracksBtn;
}
