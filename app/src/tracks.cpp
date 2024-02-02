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
    if(!track->routes.empty() || track->tracks.empty())
      populateWaypoints(track);
    else
      populateStats(track);
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

  ColorPicker* colorBtn = createColorPicker(app->markerColors, parseColor(track->style, Color::BLUE));
  colorBtn->onColor = [this, track](Color color){
    char buff[64];
    SvgWriter::serializeColor(buff, color);
    track->style = buff;
    if(track->marker > 0) {
      app->map->markerSetProperties(track->marker, {{{"color", buff}}});
      app->map->markerSetStylingFromPath(track->marker, "layers.track.draw.track");  // force refresh
    }
    if(track->rowid >= 0)
      SQLiteStmt(app->bkmkDB, "UPDATE tracks SET style = ? WHERE rowid = ?;").bind(buff, track->rowid).exec();
  };
  container->addWidget(colorBtn);

  if(track->rowid >= 0) {
    Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowMenu->addItem(track->archived ? "Unarchive" : "Archive", [=](){
      std::string q2 = fstring("UPDATE tracks SET archived = %d WHERE rowid = ?;", track->archived ? 0 : 1);
      SQLiteStmt(app->bkmkDB, q2).bind(track->rowid).exec();
      track->archived = !track->archived;
      if(!track->archived) tracksDirty = true;
      app->gui->deleteWidget(item);
    });

    overflowMenu->addItem("Delete", [=](){
      SQLiteStmt(app->bkmkDB, "DELETE FROM tracks WHERE rowid = ?").bind(track->rowid).exec();
      for(auto it = tracks.begin(); it != tracks.end(); ++it) {
        if(it->rowid == track->rowid) {
          removeTrackMarkers(track);
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
  const char* query = "SELECT rowid, title, filename, strftime('%Y-%m-%d', timestamp, 'unixepoch'), style"
      " FROM tracks WHERE archived = ? ORDER BY title;";
  SQLiteStmt(app->bkmkDB, query).bind(archived).exec(
      [&](int rowid, const char* title, const char* filename, const char* date, const char* style) {
    tracks.emplace_back(title, date, filename);
    tracks.back().rowid = rowid;
    tracks.back().style = style ? style : "";
    tracks.back().archived = archived;
  });
}

void MapsTracks::populateArchived()
{
  if(!archiveLoaded)
    loadTracks(true);
  archiveLoaded = true;

  app->gui->deleteContents(archivedContent, ".listitem");
  for(GpxFile& track : tracks) {
    if(track.archived)
      archivedContent->addWidget(createTrackEntry(&track));
  }
}

void MapsTracks::populateTracks()
{
  tracksDirty = false;
  std::vector<std::string> order = tracksContent->getOrder();
  if(order.empty()) {
    for(const auto& key : app->config["tracks"]["list_order"])
      order.push_back(key.Scalar());
  }
  tracksContent->clear();

  if(recordTrack)
    tracksContent->addItem("rec", createTrackEntry(&recordedTrack));
  for(GpxFile& track : tracks) {
    if(!track.archived)
      tracksContent->addItem(std::to_string(track.rowid), createTrackEntry(&track));
  }
  Button* item = createListItem(MapsApp::uiIcon("archive"), "Archived Tracks");
  item->onClicked = [this](){ app->showPanel(archivedPanel, true);  populateArchived(); };
  tracksContent->addItem("archved", item);
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
    app->map->setCameraPositionEased(app->map->getEnclosingCameraPosition(minLngLat, maxLngLat, {32}), 0.5f);
  }
}

void MapsTracks::populateStats(GpxFile* track)
{
  app->showPanel(statsPanel, true);
  static_cast<TextLabel*>(statsPanel->selectFirst(".panel-title"))->setText(track->title.c_str());

  showTrack(track, true);
  bool isRecTrack = track == &recordedTrack;
  pauseRecordBtn->setVisible(isRecTrack);
  stopRecordBtn->setVisible(isRecTrack);
  if(!isRecTrack)
    app->map->markerSetStylingFromPath(track->marker, "layers.selected-track.draw.track");

  if(track->activeWay())
    updateStats(track->activeWay()->pts);  // has to be done before TrackPlot::setTrack()
  if(activeTrack != track) {
    viewEntireTrack(track);
    activeTrack = track;
    retryBtn->setVisible(false);
  }
  trackPlot->setTrack(track->activeWay()->pts, track->waypoints);
}

void MapsTracks::updateStats(std::vector<Waypoint>& locs)
{
  static const char* notime = u8"\u2014";  // emdash
  //auto& locs = track->activeWay()->pts;
  //if(!origLocs.empty())
  locs.front().dist = 0;
  // how to calculate max speed?
  double trackDist = 0, trackAscent = 0, trackDescent = 0, ascentTime = 0, descentTime = 0, movingTime = 0;
  double currSpeed = 0, maxSpeed = 0;
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

    //if(!origLocs.empty())  // track has been modified - recalc distances
    locs[ii].dist = trackDist;
  }

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
  std::string timeStr = fstring("%dh %02dm %02ds", hours, mins, secs);
  statsContent->selectFirst(".track-time")->setText(timeStr.c_str());
  sparkStats->selectFirst(".spark-time")->setText(timeStr.c_str());

  ttot = movingTime;
  hours = int(ttot/3600);
  mins = int((ttot - hours*3600)/60);
  secs = int(ttot - hours*3600 - mins*60);
  statsContent->selectFirst(".track-moving-time")->setText(fstring("%dh %02dm %02ds", hours, mins, secs).c_str());

  double distUser = app->metricUnits ? trackDist/1000 : trackDist*0.000621371;
  std::string distStr = fstring(app->metricUnits ? "%.2f km" : "%.2f mi", distUser);
  statsContent->selectFirst(".track-dist")->setText(distStr.c_str());
  sparkStats->selectFirst(".spark-dist")->setText(distStr.c_str());

  std::string avgSpeedStr = fstring(app->metricUnits ? "%.2f km/h" : "%.2f mph", distUser/(movingTime/3600));
  statsContent->selectFirst(".track-avg-speed")->setText(ttot > 0 ? avgSpeedStr.c_str() : notime);

  std::string ascentStr = app->metricUnits ? fstring("%.0f m", trackAscent) : fstring("%.0f ft", trackAscent*3.28084);
  statsContent->selectFirst(".track-ascent")->setText(ascentStr.c_str());
  sparkStats->selectFirst(".spark-ascent")->setText(("+" + ascentStr).c_str());

  std::string ascentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackAscent/(ascentTime/3600))
      : fstring("%.0f ft/h", (trackAscent*3.28084)/(ascentTime/3600));
  statsContent->selectFirst(".track-ascent-speed")->setText(ascentTime > 0 ? ascentSpdStr.c_str() : notime);

  std::string descentStr = app->metricUnits ? fstring("%.0f m", trackDescent) : fstring("%.0f ft", trackDescent*3.28084);
  statsContent->selectFirst(".track-descent")->setText(descentStr.c_str());
  sparkStats->selectFirst(".spark-descent")->setText(descentStr.c_str());

  std::string descentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackDescent/(descentTime/3600))
      : fstring("%.0f ft/h", (trackDescent*3.28084)/(descentTime/3600));
  statsContent->selectFirst(".track-descent-speed")->setText(descentTime > 0 ? descentSpdStr.c_str() : notime);

  if(wayptPanel->isVisible()) {
    sparkStats->setVisible(locs.size() > 1);
    trackSpark->setTrack(locs);
  }
}

static std::string distKmToStr(double distkm)
{
  if(MapsApp::metricUnits)
    return fstring(distkm < 1 ? "%.0f m" : "%.2f km", distkm < 1 ? distkm*1000 : distkm);
  else if(distkm*0.621371 < 0.1)
    return fstring("%.0f ft", distkm*1000*3.28084);
  else
    return fstring("%.2f mi", distkm*0.621371);
}

void MapsTracks::updateDistances()
{
  if(activeTrack->routes.empty()) return;
  auto& route = activeTrack->routes.back().pts;
  if(route.empty()) return;
  auto wayPtItems = wayptContent->select(".listitem");  //->content->containerNode()->children();

  if(activeTrack->routeMode == "direct") {
    size_t rteidx = 0;
    for(Widget* item : wayPtItems) {
      auto it = activeTrack->findWaypoint(item->node->getStringAttr("__sortkey"));
      if(!it->routed) continue;
      while(rteidx < route.size()-1 && !(route[rteidx].lngLat() == it->lngLat())) ++rteidx;
      TextLabel* detail = static_cast<TextLabel*>(item->selectFirst(".detail-text"));
      std::string s = distKmToStr(route[rteidx].dist/1000);
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
    std::string s = distKmToStr(distkm);
    if(!it->desc.empty())
      s.append(u8" \u2022 ").append(it->desc);  // or \u00B7
    detail->setText(s.c_str());
    it->dist = rtePt->dist;  // for stats plot
  }
}

void MapsTracks::createRoute(GpxFile* track)
{
  retryBtn->setVisible(false);
  track->routes.clear();
  track->modified = true;
  if(track->routeMode == "direct") {
    track->routes.emplace_back();
    for(Waypoint& wp : track->waypoints) {
      if(wp.routed)
        track->routes.back().pts.push_back(wp);
    }
    updateStats(track->routes.back().pts);
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
  updateStats(activeTrack->routes.back().pts);
  updateDistances();
  updateTrackMarker(activeTrack);
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
  if(it->marker > 0)
    app->map->markerRemove(it->marker);
  if(track == activeTrack && it->uid == insertionWpt)
    insertionWpt.clear();
  track->waypoints.erase(it);
  track->modified = true;
  if(track->waypoints.empty())
    app->map->markerSetVisible(previewMarker, false);
  if(routed)
    createRoute(track);
  wayptContent->deleteItem(uid);
}

void MapsTracks::removeTrackMarkers(GpxFile* track)
{
  if(track->marker > 0)
    app->map->markerRemove(track->marker);
  track->marker = 0;
  for(auto& wpt: track->waypoints) {
    if(wpt.marker > 0)
      app->map->markerRemove(wpt.marker);
    wpt.marker = 0;
  }
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
    app->setPickResult(app->pickResultCoord, it->name, app->pickResultProps);  // must be called last
    //static_cast<TextLabel*>(app->infoPanel->selectFirst(".panel-title"))->setText(titleEdit->text().c_str());
    //noteText->setText(noteEdit->text().c_str());
    //noteText->setVisible(true);
  };
  auto onCancelEdit = [=](){ noteText->setVisible(true); };
  auto editContent = createInlineDialog({titleEdit, noteEdit}, "Apply", onAcceptEdit, onCancelEdit);

  Widget* toolRow = createRow();
  Button* chooseListBtn = createToolbutton(MapsApp::uiIcon("waypoint"), track->title.c_str(), true);
  Button* removeBtn = createToolbutton(MapsApp::uiIcon("discard"), "Delete");
  Button* addNoteBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit");

  chooseListBtn->onClicked = [=](){ populateWaypoints(track); };

  removeBtn->onClicked = [=](){
    removeWaypoint(track, uid);
    section->setVisible(false);
  };

  addNoteBtn->onClicked = [=](){
    showInlineDialogModal(editContent);
    noteText->setVisible(false);
    app->gui->setFocused(titleEdit->text().empty() ? titleEdit : noteEdit);
  };

  Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More options");
  Menu* overflowMenu = createMenu(Menu::VERT_LEFT);
  overflowBtn->setMenu(overflowMenu);

  overflowMenu->addItem("Add waypoints before", [=](){
    if(track != activeTrack)
      populateWaypoints(track);
    insertionWpt = uid;
  });

  overflowMenu->addItem("Add waypoints after", [=](){
    if(track != activeTrack)
      populateWaypoints(track);
    auto it = track->findWaypoint(uid);
    if(it != track->waypoints.end()) ++it;
    insertionWpt = it != track->waypoints.end() ? it->uid : "";
  });

  toolRow->addWidget(chooseListBtn);
  toolRow->addWidget(createStretch());
  toolRow->addWidget(removeBtn);
  toolRow->addWidget(addNoteBtn);
  toolRow->addWidget(overflowBtn);

  section->addWidget(toolRow);
  section->addWidget(noteText);
  section->addWidget(editContent);

  //return section;
  app->infoContent->selectFirst(".waypt-section")->addWidget(section);
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
  // we want to show all points in measure mode
  if(it->name.empty() && activeTrack == &navRoute && navRoute.routeMode == "direct")
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

  if(activeTrack->waypoints.size() == 1 && it->name.empty())
    it->name = "Start";
  if(showAllWaypts || !it->name.empty())
    addWaypointItem(*it, insertionWpt);
  if(replaceWaypt && !insertionWpt.empty()) {
    removeWaypoint(activeTrack, insertionWpt);
    insertionWpt.clear();
  }
  if(it->routed)
    createRoute(activeTrack);
  return &(*it);
}

// if other panels end up needing this, use onMapEvent(PICK_RESULT) instead
bool MapsTracks::onPickResult()
{
  if(!activeTrack || !(tapToAddWaypt || stealPickResult || replaceWaypt))
    return false;
  // make lat,lng markers nameless so they can be hidden easily
  addWaypoint({app->pickResultCoord, app->pickResultOsmId.empty() ? "" : app->pickResultName});
  while(app->panelHistory.back() != wayptPanel && app->popPanel()) {}
  return true;
}

bool MapsTracks::tapEvent(LngLat location)
{
  if(!activeTrack || !tapToAddWaypt || stealPickResult || replaceWaypt)
    return false;
  addWaypoint({location, ""});
  return true;
}

void MapsTracks::addWaypointItem(Waypoint& wp, const std::string& nextuid)
{
  std::string uid = wp.uid;
  std::string wpname = wp.name.empty() ? fstring("%.6f, %.6f", wp.loc.lat, wp.loc.lng) : wp.name;
  const char* desc = wp.desc.empty() ? " " : wp.desc.c_str();  // make sure detail text is non-empty
  Button* item = createListItem(MapsApp::uiIcon("waypoint"), wpname.c_str(), desc);
  Widget* container = item->selectFirst(".child-container");
  //Button* showBtn = createToolbutton(MapsApp::uiIcon("eye"), "Show");
  Button* routeBtn = createToolbutton(MapsApp::uiIcon("track"), "Route");
  Button* discardBtn = createToolbutton(MapsApp::uiIcon("discard"), "Remove");
  //container->addWidget(showBtn);
  container->addWidget(routeBtn);
  container->addWidget(discardBtn);

  item->onClicked = [=](){
    if(activeTrack == &navRoute && navRoute.routeMode != "direct") {
      replaceWaypt = true;
      insertionWpt = wp.uid;
      app->showPanel(app->mapsSearch->searchPanel, true);
    }
    else {
      auto it = activeTrack->findWaypoint(uid);
      bool wasTapToAdd = std::exchange(tapToAddWaypt, false);
      app->setPickResult(it->lngLat(), it->name, "");
      tapToAddWaypt = wasTapToAdd;
      setPlaceInfoSection(activeTrack, *it);
    }
  };

  routeBtn->setChecked(wp.routed);
  routeBtn->onClicked = [=](){
    auto it = activeTrack->findWaypoint(uid);
    it->routed = !it->routed;
    routeBtn->setChecked(it->routed);
    createRoute(activeTrack);
  };

  discardBtn->onClicked = [=](){ removeWaypoint(activeTrack, uid); };

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

void MapsTracks::populateWaypoints(GpxFile* track)
{
  waypointsDirty = false;
  app->showPanel(wayptPanel, true);
  static_cast<TextLabel*>(wayptPanel->selectFirst(".panel-title"))->setText(track->title.c_str());

  showTrack(track, true);
  directRoutePreview = true;
  app->crossHair->setVisible(true);
  if(activeTrack != track) {
    activeTrack = track;
    insertionWpt.clear();
    viewEntireTrack(track);
    app->map->markerSetStylingFromPath(track->marker, "layers.selected-track.draw.track");
    setRouteMode(activeTrack->routeMode);
    if(!track->routes.empty())
      updateStats(track->routes.back().pts);  // spark stats
    retryBtn->setVisible(false);
  }

  wayptContent->clear();
  for(Waypoint& wp : track->waypoints) {
    if(!wp.name.empty() || showAllWaypts)
      addWaypointItem(wp);
  }
  updateDistances();
}

bool MapsTracks::findPickedWaypoint(GpxFile* track)
{
  for(auto& wpt : track->waypoints) {
    if(app->pickedMarkerId == wpt.marker) {
      bool wasTapToAdd = std::exchange(tapToAddWaypt, false);
      app->setPickResult(wpt.lngLat(), wpt.name, "");
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
  if(event == SUSPEND) {
    std::vector<std::string> order = tracksContent->getOrder();
    if(order.empty()) return;
    YAML::Node ordercfg = app->config["tracks"]["list_order"] = YAML::Node(YAML::NodeType::Sequence);
    for(const std::string& s : order)
      ordercfg.push_back(s);
    return;
  }

  if(app->pickedMarkerId > 0) {
    if(app->pickedMarkerId == trackHoverMarker) {
      app->setPickResult(trackHoverLoc.lngLat(), "", "");  //activeTrack->title + " waypoint"
      app->pickedMarkerId = 0;
    }
    else if(activeTrack && app->pickedMarkerId == activeTrack->marker) {
      if(statsPanel->isVisible()) {
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
          if(track.visible && &track != activeTrack && findPickedWaypoint(&track))
            break;
        }
      }
    }
  }

  if(!activeTrack)
    return;
  if(event == LOC_UPDATE)
    updateLocation(app->currLocation);
  else if(event == SUSPEND && activeTrack->modified)
    activeTrack->modified = !saveGPX(activeTrack);
  if(event != MAP_CHANGE)
    return;

  // update polyline marker in direct mode
  if(directRoutePreview && activeTrack->routeMode == "direct" && !activeTrack->waypoints.empty()) {
    auto it = insertionWpt.empty() ? activeTrack->waypoints.end() : activeTrack->findWaypoint(insertionWpt);
    std::vector<LngLat> pts = {(--it)->lngLat(), app->getMapCenter()};
    double distkm = lngLatDist(pts[0], pts[1]);
    double pix = distkm*1000/MapProjection::metersPerPixelAtZoom(app->map->getZoom());
    if(previewMarker <= 0) {
      previewMarker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(previewMarker, "layers.track.draw.track");  //styling);
    }
    if(previewRoute.empty() || !(pts[0] == previewRoute[0] && pts[1] == previewRoute[1])) {
      app->map->markerSetPolyline(previewMarker, pts.data(), 2);
      app->map->markerSetProperties(previewMarker, {{{"color", "red"}}});
      app->map->markerSetVisible(previewMarker, pix > 2);
    }
    previewRoute = pts;
    previewDistText->setText(distKmToStr(distkm).c_str());
  }
}

void MapsTracks::setRouteMode(const std::string& mode)
{
  auto parts = splitStr<std::vector>(mode.c_str(), '-');
  const char* icon = "segment";
  if(parts[0] == "walk") icon = "walk";
  else if(parts[0] == "bike") icon = "bike";
  else if(parts[0] == "drive") icon = "car";
  routeModeBtn->setIcon(MapsApp::uiIcon(icon));
  if(directRoutePreview) {
    previewRoute.clear();
    if(mode != "direct")
      app->map->markerSetVisible(previewMarker, false);
    else
      onMapEvent(MAP_CHANGE);
  }
  if(!activeTrack || activeTrack->routeMode == mode) return;
  activeTrack->routeMode = mode;
  createRoute(activeTrack);
};

void MapsTracks::addPlaceActions(Toolbar* tb)
{
  if(activeTrack) {
    Button* addWptBtn = createToolbutton(MapsApp::uiIcon("waypoint"), "Add waypoint");
    addWptBtn->onClicked = [=](){
      Waypoint* wpt = addWaypoint({app->pickResultCoord, app->pickResultOsmId.empty() ? "" : app->pickResultName});
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
      removeTrackMarkers(&navRoute);
      navRoute.title = "Navigation";
      navRoute.routeMode = km < 10 ? "walk" : km < 100 ? "bike" : "drive";
      navRoute.waypoints.clear();
      navRoute.wayPtSerial = 0;
      if(km > 0.01)
        navRoute.addWaypoint({r1, "Start"});  //"Current location"
      navRoute.addWaypoint({r2, app->pickResultName});
      activeTrack = NULL;
      createRoute(&navRoute);
      populateWaypoints(&navRoute);
    };
    tb->addWidget(routeBtn);

    Button* measureBtn = createToolbutton(MapsApp::uiIcon("measure"), "Directions");
    measureBtn->onClicked = [=](){
      removeTrackMarkers(&navRoute);
      navRoute.title = "Measurement";
      navRoute.routeMode = "direct";
      navRoute.routes.clear();
      navRoute.waypoints.clear();
      navRoute.wayPtSerial = 0;
      navRoute.addWaypoint({app->pickResultCoord, app->pickResultName});
      activeTrack = NULL;
      populateWaypoints(&navRoute);
    };
    tb->addWidget(measureBtn);
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

void MapsTracks::updateDB(GpxFile* track)
{
  if(track->filename.empty())
    track->filename = FSPath(MapsApp::baseDir, "tracks/" + track->title + ".gpx").c_str();
  if(track->rowid < 0) {
    SQLiteStmt(app->bkmkDB, "INSERT INTO tracks (title,filename) VALUES (?,?);")
        .bind(track->title, track->filename).exec();
    track->rowid = sqlite3_last_insert_rowid(app->bkmkDB);
  }
  else
    SQLiteStmt(app->bkmkDB, "UPDATE tracks SET title = ?, filename = ? WHERE rowid = ?;")
        .bind(track->title, track->filename, track->rowid).exec();
  track->loaded = true;
  tracksDirty = true;
}

Button* MapsTracks::createPanel()
{
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS tracks(title TEXT, filename TEXT, style TEXT,"
      " timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)), archived INTEGER DEFAULT 0);");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS tracks_state(track_id INTEGER, ordering INTEGER, visible INTEGER);");

  // Stats panel
  statsContent = createColumn();

  Button* editTrackBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit Track");

  TextEdit* saveTitle = createTitledTextEdit("Title");
  TextEdit* savePath = createTitledTextEdit("Path");
  CheckBox* replaceTrackCb = createCheckBox("Replace track", false);

  auto onSaveTrackCancel = [=](){ if(!recordTrack) editTrackBtn->onClicked(); };
  auto onSaveTrack = [=](){
    std::string prevFile = activeTrack->filename;
    activeTrack->title = saveTitle->text();
    activeTrack->filename = savePath->text();
    if(saveGPX(activeTrack)) {
      bool replace = replaceTrackCb->isChecked();
      if(activeTrack->rowid >= 0)
        updateDB(activeTrack);
      if(replace && !prevFile.empty() && prevFile != activeTrack->filename)
        removeFile(prevFile);
      origLocs.clear();
      if(!recordTrack)
        editTrackBtn->onClicked();  // close edit track view
    }
    else
      activeTrack->filename = prevFile;
    tracksDirty = true;
  };

  Widget* saveTrackContent = createInlineDialog(
      {saveTitle, savePath, replaceTrackCb}, "Apply", onSaveTrack, onSaveTrackCancel);
  Button* saveTrackBtn = static_cast<Button*>(saveTrackContent->selectFirst(".accept-btn"));

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

  statsContent->addWidget(saveTrackContent);

  // bearing? direction (of travel)?
  statsContent->addWidget(createStatsRow({"Latitude", "track-latitude", "Longitude", "track-longitude"}));
  statsContent->addWidget(createStatsRow({"Altitude", "track-altitude", "Speed", "track-speed"}));
  //statsContent->addWidget(createStatsRow({"Position", "track-position", "Altitude", "track-altitude", "Speed", "track-speed"}));
  statsContent->addWidget(createStatsRow({"Total time", "track-time", "Moving time", "track-moving-time"}));
  statsContent->addWidget(createStatsRow({"Distance", "track-dist", "Avg speed", "track-avg-speed"}));
  statsContent->addWidget(createStatsRow({"Ascent", "track-ascent", "Descent", "track-descent"}));
  statsContent->addWidget(createStatsRow({"Ascent speed", "track-ascent-speed", "Descent speed", "track-descent-speed"}));

  Button* vertAxisSelBtn = createToolbutton(NULL, "Altitude", true);
  Menu* vertAxisMenu = createMenu(Menu::VERT_LEFT);
  vertAxisSelBtn->setMenu(vertAxisMenu);
  Button* plotAltiBtn = createCheckBoxMenuItem("Altitude");
  Button* plotSpeedBtn = createCheckBoxMenuItem("Speed");
  vertAxisMenu->addWidget(plotAltiBtn);
  vertAxisMenu->addWidget(plotSpeedBtn);
  plotAltiBtn->setChecked(true);

  plotAltiBtn->onClicked = [=](){
    plotAltiBtn->setChecked(!plotAltiBtn->isChecked());
    trackPlot->plotAlt = plotAltiBtn->isChecked();
    bool alt = trackPlot->plotAlt, spd = trackPlot->plotSpd;
    if(!alt && !spd) {
      trackPlot->plotSpd = true;
      plotSpeedBtn->setChecked(true);
    }
    vertAxisSelBtn->setTitle(alt && spd ? "Altitude, Speed" : alt ? "Altitude" : "Speed");
    trackPlot->redraw();
  };

  plotSpeedBtn->onClicked = [=](){
    plotSpeedBtn->setChecked(!plotSpeedBtn->isChecked());
    trackPlot->plotSpd = plotSpeedBtn->isChecked();
    bool alt = trackPlot->plotAlt, spd = trackPlot->plotSpd;
    if(!alt && !spd) {
      trackPlot->plotAlt = true;
      plotAltiBtn->setChecked(true);
    }
    vertAxisSelBtn->setTitle(alt && spd ? "Altitude, Speed" : spd ? "Speed" : "Altitude");
    trackPlot->redraw();
  };

  Button* horzAxisSelBtn = createToolbutton(NULL, "Distance", true);
  Button* plotVsDistBtn = createCheckBoxMenuItem("Distance", "#radiobutton");
  Button* plotVsTimeBtn = createCheckBoxMenuItem("Time", "#radiobutton");
  Menu* horzAxisMenu = createMenu(Menu::VERT_LEFT);
  horzAxisSelBtn->setMenu(horzAxisMenu);
  horzAxisMenu->addWidget(plotVsDistBtn);
  horzAxisMenu->addWidget(plotVsTimeBtn);
  plotVsDistBtn->setChecked(true);

  auto horzAxisSelFn = [=](bool vsdist){
    trackPlot->plotVsDist = vsdist;
    plotVsDistBtn->setChecked(vsdist);
    plotVsTimeBtn->setChecked(!vsdist);
    horzAxisSelBtn->setTitle(vsdist ? "Distance" : "Time");
    trackPlot->redraw();
  };
  plotVsTimeBtn->onClicked = [=](){ horzAxisSelFn(false); };
  plotVsDistBtn->onClicked = [=](){ horzAxisSelFn(true); };

  Toolbar* axisSelRow = createToolbar();
  axisSelRow->addWidget(vertAxisSelBtn);
  axisSelRow->addWidget(new TextBox(createTextNode("vs.")));
  axisSelRow->addWidget(horzAxisSelBtn);
  statsContent->addWidget(axisSelRow);

  trackPlot = new TrackPlot();
  trackPlot->node->setAttribute("box-anchor", "hfill");
  trackPlot->setMargins(1, 5);

  statsContent->addWidget(trackPlot);

  trackSliders = createTrackSliders();
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

  Button* deleteSegmentBtn = createToolbutton(NULL, "Delete Segment", true);
  deleteSegmentBtn->onClicked = [=](){
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
  };

  Button* moreTrackOptionsBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More options");
  Menu* trackPlotOverflow = createMenu(Menu::VERT_LEFT);
  moreTrackOptionsBtn->setMenu(trackPlotOverflow);

  trackPlotOverflow->addItem("Append track", [this](){
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
        selectTrackDialog->addItems({track.title}, false);
    }
    showModalCentered(selectTrackDialog.get(), MapsApp::gui);
  });

  trackPlotOverflow->addItem("Reverse Track", [this](){
    if(origLocs.empty()) origLocs = activeTrack->tracks.front().pts;
    auto& locs = activeTrack->tracks.front().pts;
    std::reverse(locs.begin(), locs.end());
    updateTrackMarker(activeTrack);  // rebuild marker
    populateStats(activeTrack);
  });

  Toolbar* editTrackTb = createToolbar();
  editTrackTb->addWidget(cropTrackBtn);
  editTrackTb->addWidget(deleteSegmentBtn);
  editTrackTb->addWidget(createStretch());
  editTrackTb->addWidget(moreTrackOptionsBtn);
  editTrackTb->setVisible(false);
  statsContent->addWidget(editTrackTb);

  //Toolbar* trackOptionsTb = createToolbar();
  //trackOptionsTb->addWidget(createBkmkBtn);
  //trackOptionsTb->setVisible(false);
  //statsContent->addWidget(trackOptionsTb);

  pauseRecordBtn = createToolbutton(MapsApp::uiIcon("pause"), "Pause");
  stopRecordBtn = createToolbutton(MapsApp::uiIcon("stop"), "Stop");

  Button* statsWayptsBtn = createToolbutton(MapsApp::uiIcon("waypoint"), "Edit Waypoints");
  statsWayptsBtn->onClicked = [=](){ GpxFile* t = activeTrack; app->popPanel(); populateWaypoints(t); };

  auto setTrackEdit = [=](bool show){
    editTrackTb->setVisible(show);
    trackSliders->setEditMode(show);
    app->map->markerSetVisible(trackHoverMarker, !show);
    if(show)
      trackSliders->setCropHandles(0, 1, TrackSliders::FORCE_UPDATE);
    else {
      app->map->markerSetVisible(trackStartMarker, false);
      app->map->markerSetVisible(trackEndMarker, false);
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
    updateDB(&recordedTrack);
    tracks.push_back(std::move(recordedTrack));
    recordedTrack = GpxFile();
    recordTrack = false;
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

  auto hoverFn = [this](real s){
    if(s < 0 || s > 1 || !activeTrack) {
      app->map->markerSetVisible(trackHoverMarker, false);
      return;
    }
    trackHoverLoc = interpTrack(activeTrack->activeWay()->pts, s);
    if(trackHoverMarker == 0) {
      trackHoverMarker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(trackHoverMarker, "layers.track-marker.draw.marker");
    }
    app->map->markerSetVisible(trackHoverMarker, true);
    app->map->markerSetPoint(trackHoverMarker, trackHoverLoc.lngLat());
    // or fixed color? or yellow?
    app->map->markerSetProperties(trackHoverMarker, {{{"color", activeTrack->style}}});
  };
  //trackPlot->onHovered = hoverFn;
  trackSliders->onValueChanged = hoverFn;

  trackPlot->onPanZoom = [=](){
    real start = trackPlot->trackPosToPlotPos(cropStart);
    real end = trackPlot->trackPosToPlotPos(cropEnd);
    trackSliders->setCropHandles(start, end, TrackSliders::NO_UPDATE);
  };

  // Tracks panel
  Widget* tracksContainer = createColumn();
  tracksContainer->node->setAttribute("box-anchor", "fill");
  tracksContent = new DragDropList;  //createColumn();

  Button* drawTrackBtn = createToolbutton(MapsApp::uiIcon("draw-track"), "Create Route");
  TextEdit* newTrackTitle = createTitledTextEdit("Title");
  TextEdit* newTrackFile = createTitledTextEdit("File");
  Widget* newTrackContent = createInlineDialog({newTrackTitle, newTrackFile}, "Create", [=](){
    tracks.emplace_back(newTrackTitle->text(), ftimestr("%F"), newTrackFile->text());
    updateDB(&tracks.back());
    populateWaypoints(&tracks.back());
    drawTrackBtn->setChecked(false);
  }, [=](){ drawTrackBtn->setChecked(false); });
  newTrackTitle->onChanged = [=](const char* s){ newTrackContent->selectFirst(".accept-btn")->setEnabled(s[0]); };
  tracksContainer->addWidget(newTrackContent);
  tracksContainer->addWidget(tracksContent);

  drawTrackBtn->onClicked = [=](){
    newTrackTitle->setText(ftimestr("%FT%H.%M.%S").c_str());  //"%Y-%m-%d %HH%M"
    showInlineDialogModal(newTrackContent);
    drawTrackBtn->setChecked(true);
  };

  Button* loadTrackBtn = createToolbutton(MapsApp::uiIcon("open-folder"), "Load Track");
  auto loadTrackFn = [=](const char* outPath){
    tracks.emplace_back("", "", outPath);
    loadGPX(&tracks.back());
    if(tracks.back().waypoints.empty() && !tracks.back().activeWay()) {
      PLATFORM_LOG("Error loading track!");
      tracks.pop_back();
      return;
    }
    updateDB(&tracks.back());
    populateStats(&tracks.back());
  };
  loadTrackBtn->onClicked = [=](){ MapsApp::openFileDialog({{"GPX files", "gpx"}}, loadTrackFn); };

  Button* recordTrackBtn = createToolbutton(MapsApp::uiIcon("record"), "Record Track");
  recordTrackBtn->onClicked = [=](){
    if(!recordedTrack.tracks.empty())
      populateStats(&recordedTrack);  // show stats panel for recordedTrack, incl pause and stop buttons
    else {
      recordTrack = true;
      std::string timestr = ftimestr("%FT%H.%M.%S");
      FSPath gpxPath(app->baseDir, "tracks/" + timestr + ".gpx");
      recordedTrack = GpxFile(timestr, "", gpxPath.path);  //Track{timestr, "", gpxPath.c_str(), "", 0, {}, -1, true, false};
      recordedTrack.loaded = true;
      recordedTrack.tracks.emplace_back();
      recordedTrack.tracks.back().pts.push_back(app->currLocation);
      recordedTrack.marker = app->map->markerAdd();
      app->map->markerSetStylingFromPath(recordedTrack.marker, "layers.recording-track.draw.track");
      tracksDirty = true;
      populateStats(&recordedTrack);
      saveTitle->setText(recordedTrack.title.c_str());
      savePath->setText(recordedTrack.filename.c_str());
      saveTrackContent->setVisible(true);
    }
  };

  auto statsTb = app->createPanelHeader(MapsApp::uiIcon("graph-line"), "");
  statsTb->addWidget(pauseRecordBtn);
  statsTb->addWidget(stopRecordBtn);
  statsTb->addWidget(statsWayptsBtn);
  statsTb->addWidget(editTrackBtn);
  statsPanel = app->createMapPanel(statsTb, statsContent);
  statsPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == MapsApp::PANEL_CLOSED) {
      if(editTrackBtn->isChecked())
        editTrackBtn->onClicked();
      app->map->markerSetVisible(trackHoverMarker, false);
      if(activeTrack && app->panelHistory.back() != wayptPanel) {
        if(activeTrack != &recordedTrack)
          app->map->markerSetStylingFromPath(activeTrack->marker, "layers.track.draw.track");
        if(!activeTrack->visible)
          showTrack(activeTrack, false);
        activeTrack = NULL;
      }
    }
    return false;
  });

  auto tracksTb = app->createPanelHeader(MapsApp::uiIcon("track"), "Tracks");
  tracksTb->addWidget(drawTrackBtn);
  tracksTb->addWidget(loadTrackBtn);
  tracksTb->addWidget(recordTrackBtn);
  tracksPanel = app->createMapPanel(tracksTb, NULL, tracksContainer, false);

  tracksPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      if(tracksDirty)
        populateTracks();
    }
    return false;
  });

  archivedContent = createColumn();
  auto archivedHeader = app->createPanelHeader(MapsApp::uiIcon("archive"), "Archived Tracks");
  archivedPanel = app->createMapPanel(archivedHeader, archivedContent, NULL, false);

  // waypoint panel
  Widget* wayptContainer = createColumn();
  wayptContainer->node->setAttribute("box-anchor", "fill");
  wayptContent = new DragDropList;

  TextEdit* editRouteTitle = createTitledTextEdit("Title");
  Widget* editRouteContent = createInlineDialog({editRouteTitle}, "Save", [=](){
    activeTrack->title = editRouteTitle->text();
    updateDB(activeTrack);
    populateWaypoints(activeTrack);
  });
  editRouteTitle->onChanged = [=](const char* s){ editRouteContent->selectFirst(".accept-btn")->setEnabled(s[0]); };

  TextEdit* saveRouteTitle = createTitledTextEdit("Title");
  TextEdit* saveRouteFile = createTitledTextEdit("File");
  Widget* saveRouteContent = createInlineDialog({saveRouteTitle, saveRouteFile}, "Create", [=](){
    tracks.push_back(std::move(navRoute));
    tracks.back().title = saveRouteTitle->text();
    tracks.back().desc = ftimestr("%F");
    tracks.back().filename = saveRouteFile->text();
    updateDB(&tracks.back());
    populateWaypoints(&tracks.back());
  });
  saveRouteTitle->onChanged = [=](const char* s){ saveRouteContent->selectFirst(".accept-btn")->setEnabled(s[0]); };

  Button* saveRouteBtn = createToolbutton(MapsApp::uiIcon("save"), "Save");
  saveRouteBtn->onClicked = [=](){
    if(activeTrack->filename.empty()) {
      saveRouteTitle->setText("");
      saveRouteFile->setText("");
      showInlineDialogModal(saveRouteContent);
    }
    else
      activeTrack->modified = !saveGPX(activeTrack);
  };

  bool hasPlugins = !app->pluginManager->routeFns.empty();
  Button* routePluginBtn = createToolbutton(MapsApp::uiIcon(hasPlugins ? "plugin" : "no-plugin"), "Plugin");
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
  routeModeMenu->addItem("Walk", MapsApp::uiIcon("walk"), [=](){ setRouteMode("walk"); });
  routeModeMenu->addItem("Cycle", MapsApp::uiIcon("bike"), [=](){ setRouteMode("bike"); });
  routeModeMenu->addItem("Drive", MapsApp::uiIcon("car"), [=](){ setRouteMode("drive"); });
  routeModeBtn->setMenu(routeModeMenu);
  routeModeBtn->setEnabled(hasPlugins);

  Button* wayptsOverflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More options");
  Menu* wayptsOverflow = createMenu(Menu::VERT_LEFT);
  wayptsOverflowBtn->setMenu(wayptsOverflow);

  Button* showAllWptsBtn = createCheckBoxMenuItem("Show untitled waypoints");
  showAllWptsBtn->onClicked = [=](){
    showAllWaypts = !showAllWaypts;
    showAllWptsBtn->setChecked(showAllWaypts);
    populateWaypoints(activeTrack);
  };
  showAllWptsBtn->setChecked(showAllWaypts);
  wayptsOverflow->addItem(showAllWptsBtn);

  wayptsOverflow->addItem("Rename", [=](){
    showInlineDialogModal(editRouteContent);
    editRouteTitle->setText(activeTrack->title.c_str());
  });

  // stats preview for route
  static const char* sparkStatsSVG = R"(
    <g layout="box" box-anchor="hfill" margin="3 3">
      <rect class="background" box-anchor="fill" width="20" height="20"/>
      <g class="spark-row-container" layout="flex" flex-direction="row" box-anchor="hfill">
        <g layout="flex" flex-direction="column" box-anchor="vfill" margin="0 2" font-size="14">
          <text class="spark-dist" box-anchor="left"></text>
          <text class="spark-time" box-anchor="left"></text>
          <text class="spark-ascent" box-anchor="left"></text>
          <text class="spark-descent" box-anchor="left"></text>
        </g>
      </g>
    </g>
  )";
  sparkStats = new Button(loadSVGFragment(sparkStatsSVG));
  trackSpark = new TrackSparkline();
  trackSpark->mBounds = Rect::wh(200, 80);
  trackSpark->node->setAttribute("box-anchor", "hfill");
  trackSpark->setMargins(1, 1);
  sparkStats->selectFirst(".spark-row-container")->addWidget(trackSpark);
  sparkStats->onClicked = [=](){ populateStats(activeTrack); };
  sparkStats->setVisible(false);  // will be shown when route has >1 point

  // I think this will eventually be a floating toolbar
  Toolbar* routeEditTb = createToolbar();

  previewDistText = new TextBox(createTextNode(""));

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

  Button* tapToAddWayptBtn = createCheckBoxMenuItem("Tap to add waypoint");
  tapToAddWayptBtn->onClicked = [=](){
    tapToAddWaypt = !tapToAddWaypt;
    tapToAddWayptBtn->setChecked(tapToAddWaypt);
  };
  tapToAddWayptBtn->setChecked(tapToAddWaypt);
  rteEditOverflow->addItem(tapToAddWayptBtn);

  routeEditTb->addWidget(previewDistText);
  routeEditTb->addWidget(createStretch());
  routeEditTb->addWidget(retryBtn);
  routeEditTb->addWidget(mapCenterWayptBtn);
  routeEditTb->addWidget(searchWayptBtn);
  routeEditTb->addWidget(bkmkWayptBtn);
  routeEditTb->addWidget(rteEditOverflowBtn);

  wayptContainer->addWidget(routeEditTb);
  wayptContainer->addWidget(editRouteContent);
  wayptContainer->addWidget(saveRouteContent);
  wayptContainer->addWidget(sparkStats);
  wayptContainer->addWidget(wayptContent);

  auto wayptsTb = app->createPanelHeader(MapsApp::uiIcon("track"), "Waypoints");
  wayptsTb->addWidget(saveRouteBtn);
  wayptsTb->addWidget(routeModeBtn);
  wayptsTb->addWidget(routePluginBtn);
  wayptsTb->addWidget(wayptsOverflowBtn);
  wayptPanel = app->createMapPanel(wayptsTb, NULL, wayptContainer);
  wayptPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      replaceWaypt = false;  // clear this when coming back from search, regardless of point choosen or not
      stealPickResult = false;
      if(waypointsDirty)
        populateWaypoints(activeTrack);
    }
    else if(event->type == MapsApp::PANEL_CLOSED) {
      app->pluginManager->cancelRequests(PluginManager::ROUTE);
      directRoutePreview = false;
      app->map->markerSetVisible(previewMarker, false);
      app->crossHair->setVisible(false);
      if(activeTrack) {
        if(activeTrack->modified)
          activeTrack->modified = !saveGPX(activeTrack);
        if(activeTrack != &recordedTrack)
          app->map->markerSetStylingFromPath(activeTrack->marker, "layers.track.draw.track");
        if(!activeTrack->visible)
          showTrack(activeTrack, false);
        activeTrack = NULL;
      }
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
        populateTracks();  // needed to get order
      auto items = tracksContent->getOrder();
      for(size_t ii = 0; ii < recordTrack ? 9 : 10 && ii < items.size(); ++ii) {
        for(size_t jj = 0; jj < tracks.size(); ++jj) {
          if(std::to_string(tracks[jj].rowid) == items[ii]) {
            Button* item = createCheckBoxMenuItem(tracks[jj].title.c_str());
            tracksMenu->addItem(item);
            SvgPainter::elideText(static_cast<SvgText*>(item->selectFirst(".title")->node), uiWidth - 100);
            item->onClicked = [jj, this](){ setTrackVisible(&tracks[jj], !tracks[jj].visible); };
            item->setChecked(tracks[jj].visible);
            break;
          }
        }
      }
    }
    return false;
  });

  Button* tracksBtn = app->createPanelButton(MapsApp::uiIcon("track"), "Tracks", tracksPanel);
  tracksBtn->setMenu(tracksMenu);
  return tracksBtn;
}
