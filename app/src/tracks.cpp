#include "tracks.h"
#include "mapsapp.h"
#include "util.h"
#include "mapwidgets.h"
#include "plugins.h"
#include "mapsearch.h"
#include "bookmarks.h"
#include "trackwidgets.h"

#include "gaml/src/yaml.h"
#include "util/yamlPath.h"
#include "util/mapProjection.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "usvg/svgpainter.h"
#include "usvg/svgparser.h"  // for parseColor
#include "usvg/svgwriter.h"  // for serializeColor


void MapsTracks::updateLocation(const Location& loc)
{
  if(!recordTrack)
    return;
  auto& locs = recordedTrack.tracks.back().pts;
  if(loc.poserr > 50 || (locs.empty() && loc.poserr > 25))
    return;
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
    if(dt <= 0) return;
    // update speed even if point is rejected
    double a = std::exp(-dt*speedInvTau);
    // GPS speed can be more accurate than dist/time due to e.g. doppler measurements of GPS signal
    double spd = std::isnan(loc.spd) ? dist/dt : loc.spd;
    currSpeed = a*currSpeed + (1-a)*spd;
    // note MapsApp::updateLocation() has already filtered out points w/ poserr increased too much vs. prev
    if(dist < minTrackDist && dt < minTrackTime && vert < minTrackDist)
      return;
    lastTrackPtTime = mSecSinceEpoch();
    plotDirty = true;
    if(loc.spd > 0) recordedTrack.hasSpeed = true;  // comparison false for NaN
    double d0 = prev.dist;
    locs.emplace_back(loc);
    locs.back().dist = d0 + dist;
    //if(std::isnan(loc.spd))
      locs.back().loc.spd = currSpeed; //dist/dt;
    if(!app->appSuspended) {
      if(recordedTrack.visible || activeTrack == &recordedTrack)
        updateTrackMarker(&recordedTrack);  // rebuild marker
      if(activeTrack == &recordedTrack || (!activeTrack && tracksPanel->isVisible()))
        updateStats(&recordedTrack);
    }
  }

  if(recordedTrack.modified || locs.size() == 1) {
    recordedTrack.modified = !saveTrack(&recordedTrack);
    return;
  }
  // append point to GPX file in case process is killed - pugixml will ignore the missing closing tags
  if(!recordGPXStrm) {
    recordGPXStrm = std::make_unique<FileStream>(recordedTrack.filename.c_str(), "rb+");
    // don't bother truncating since single <trkpt> node is over 30 bytes
    if(!recordGPXStrm->is_open()) {
      LOGE("Error opening stream for %s", recordedTrack.filename.c_str());
      assert(0);  // file not created yet?
    }
    else {
      // this is intended to prevent future changes from silently breaking track recording
      // expected tail is "</trkseg>\n</trk>\n</gpx>\n"
      std::string tail(60, ' ');
      recordGPXStrm->seek(-60, SEEK_END);
      tail.resize(recordGPXStrm->read(&tail[0], 60));
      size_t tailpos = tail.rfind("</trkseg>");
      if(tailpos == std::string::npos) {
        recordGPXStrm->close();
        LOGE("Unexpected GPX file ending: %s", tail.c_str());
        assert(0);
      }
      else
        recordGPXStrm->seek(-int(tail.size() - tailpos), SEEK_END);
    }
  }
  if(!recordGPXStrm->is_open()) return;
  PugiXMLWriter writer(*recordGPXStrm);
  pugi::xml_document xmldoc;
  pugi::xml_node trkpt = xmldoc.append_child("trkpt");
  saveWaypoint(trkpt, locs.back(), recordedTrack.hasSpeed);
  trkpt.print(writer, "  ", pugi::format_default | pugi::format_no_declaration);
}

bool MapsTracks::saveTrack(GpxFile* track)
{
  if(track == &recordedTrack)
    recordGPXStrm.reset();
  if(track == activeTrack && track->activeWay() && !track->activeWay()->pts.empty()) {
    bool istrack = track->routes.empty() && !track->tracks.empty();
    if(istrack) {
      double t0 = track->activeWay()->pts.front().loc.time;
      track->desc = (t0 > 0 ? (ftimestr("%F", t0*1000) + " | ") : "") + trackSummary;
    }
    else {
      auto parts = splitStr<std::vector>(track->routeMode.c_str(), '-');
      std::string mode = !parts.empty() && !parts[0].empty() ? parts[0] : "direct";
      mode[0] = char(toupper(mode[0]));
      track->desc = mode + " | " + trackSummary;
    }
  }
  SQLiteStmt(app->bkmkDB, "UPDATE tracks SET notes = ? WHERE rowid = ?;").bind(track->desc, track->rowid).exec();
  return saveGPX(track);
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
  if(!track->loaded && !track->filename.empty() && !loadGPX(track))
    MapsApp::messageBox("File not found", fstring("Error opening %s", track->filename.c_str()), {"OK"});

  Properties props;
  if(!track->marker)
    track->marker = std::make_unique<TrackMarker>();  //app->map.get(), "layers.track.draw.track");
  if(track->activeWay() && (track->activeWay()->pts.size() > 1 || track->routes.size() > 1)) {
    if(!track->routes.empty())
      track->marker->setTrack(&track->routes.front(), track->routes.size());
    else
      track->marker->setTrack(&track->tracks.front(), track->tracks.size());
    if(!track->style.empty())
      props.set("color", track->style);
    props.set("visible", 1);
  }
  else
    props.set("visible", 0);
  props.set("track_feature_id", track->marker->featureId);
  track->marker->setProperties(std::move(props));

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
  bool hasway = track->activeWay() && track->activeWay()->pts.size() > 1;
  track->marker->setProperties({{{"visible", show && hasway ? 1 : 0}}});
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
  auto* icon = track == &recordedTrack ? MapsApp::uiIcon("track-recording") : MapsApp::uiIcon("track");
  Button* item = createListItem(icon, track->title.c_str(), track->desc.c_str());
  item->node->setAttr("__rowid", track->rowid);
  item->onClicked = [=](){ populateTrack(track); };
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
      track->archived = !track->archived;
      std::string q2 = fstring("UPDATE tracks SET archived = %d WHERE rowid = ?;", track->archived ? 1 : 0);
      SQLiteStmt(app->bkmkDB, q2).bind(track->rowid).exec();
      if(track->archived) {
        if(track->visible)
          setTrackVisible(track, false);
        if(!archiveLoaded)
          tracks.remove_if([](const GpxFile& t){ return t.archived; });
        archiveDirty = true;
        populateTrackList();  // update archived count
      }
      else {
        tracksDirty = true;
        app->gui->deleteWidget(item);  // must be last!
      }
    });

    overflowMenu->addItem("Delete", [=](){
      setTrackVisible(track, false);
      SQLiteStmt(app->bkmkDB, "DELETE FROM tracks WHERE rowid = ?").bind(track->rowid).exec();
      // move GPX file to trash and add undelete item
      FSPath fileinfo(track->filename);
      FSPath trashinfo(MapsApp::baseDir, ".trash/" + fileinfo.fileName());
      moveFile(fileinfo, trashinfo);
      app->addUndeleteItem(track->title, MapsApp::uiIcon("track"), [=](){
        GpxFile restored("", "", trashinfo.path);
        loadGPX(&restored);
        restored.filename.clear();
        tracks.push_back(std::move(restored));
        updateDB(&tracks.back());
        saveTrack(&tracks.back());
        populateTrackList();
      });
      int rowid = track->rowid;
      // must not access track after this point
      tracks.remove_if([rowid](const GpxFile& t){ return t.rowid == rowid; });
      app->gui->deleteWidget(item);
    });
  }
  overflowBtn->setMenu(overflowMenu);
  container->addWidget(overflowBtn);
  return item;
}

void MapsTracks::loadTracks(bool archived)
{
  // order by timestamp for Archived
  const char* query = "SELECT rowid, title, filename, notes, style"  //strftime('%Y-%m-%d', timestamp, 'unixepoch')
      " FROM tracks WHERE archived = ? ORDER BY timestamp;";
  SQLiteStmt(app->bkmkDB, query).bind(archived).exec(
      [&](int rowid, std::string title, std::string filename, std::string desc, std::string style) {
    FSPath fileinfo(filename);
    if(!fileinfo.isAbsolute())
      fileinfo = FSPath(MapsApp::baseDir, filename);
    tracks.emplace_back(title, desc, fileinfo.path, style, rowid, archived);
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
    tracksContent->addItem(std::to_string(recordedTrack.rowid), createTrackEntry(&recordedTrack));
  for(GpxFile& track : tracks) {
    if(!track.archived)
      tracksContent->addItem(std::to_string(track.rowid), createTrackEntry(&track));
  }

  int narchived = 0;
  SQLiteStmt(app->bkmkDB, "SELECT COUNT(1) FROM tracks WHERE archived = 1;").onerow(narchived);
  Button* item = createListItem(MapsApp::uiIcon("archive"), "Archived",
      narchived == 1 ? "1 track" : fstring("%d tracks", narchived).c_str());
  item->onClicked = [this](){ app->showPanel(archivedPanel, true);  populateArchived(); };
  tracksContent->addItem("-1", item);
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
    app->lookAt(minLngLat, maxLngLat, 17.0f);
  }
}

void MapsTracks::closeActiveTrack()
{
  if(!activeTrack) return;
  app->pluginManager->cancelRequests(PluginManager::ROUTE);
  if(activeTrack != &recordedTrack && activeTrack->marker)
    activeTrack->marker->setProperties({{{"selected", 0}}});  //setStylePath("layers.track.draw.track");
  if(!activeTrack->visible)
    showTrack(activeTrack, false);
  if(activeTrack->rowid >= 0) {
    if(activeTrack->modified)
      activeTrack->modified = !saveTrack(activeTrack);
    Widget* item = tracksContent->getItem(std::to_string(activeTrack->rowid));
    if(item)
      item->selectFirst(".detail-text")->setText(activeTrack->desc.c_str());
  }
  activeTrack = NULL;
  waypointsDirty = false; plotDirty = false;
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

static void updateWayptCount(Widget* w, GpxFile* track)
{
  int n = int(track->waypoints.size());
  w->setText(n == 1 ? "1 waypoint" : fstring("%d waypoints", n).c_str());
}

void MapsTracks::populateTrack(GpxFile* track)  //TrackView_t view)
{
  showTrack(track, true);  // this will ensure track is loaded from GPX
  bool istrack = track->routes.empty() && !track->tracks.empty();
  bool isRecTrack = track == &recordedTrack;
  pauseRecordBtn->setVisible(isRecTrack);
  stopRecordBtn->setVisible(isRecTrack);
  saveCurrLocBtn->setVisible(isRecTrack);
  saveRouteBtn->setVisible(!istrack && track->filename.empty());

  if(activeTrack != track) {
    closeActiveTrack();
    activeTrack = track;
    viewEntireTrack(track);
    if(!isRecTrack)
      activeTrack->marker->setProperties({{{"selected", 1}}});  //track->marker->setStylePath("layers.selected-track.draw.track");
    insertionWpt.clear();
    retryBtn->setVisible(false);
    if(!istrack) {
      if(track->routeMode.empty())
        track->routeMode = "direct";
      if(!wayptWidgets[0]->isVisible())
        setTrackWidgets(TRACK_WAYPTS);
    }
    else if(wayptWidgets[0]->isVisible())
      setTrackWidgets(TRACK_PLOT);
    updateWayptCount(wayptTabLabel, track);
    waypointsDirty = true; plotDirty = true;
    trackSpark->darkMode = trackPlot->darkMode = MapsApp::config["ui"]["theme"].as<std::string>("") != "light";
    trackSliders->trackSlider->setVisible(false);  // reset track slider for new track
  }
  trackPlot->zoomScale = 1.0;
  updateStats(track);

  if(waypointsDirty && wayptWidgets[0]->isVisible()) {
    waypointsDirty = false;
    setRouteMode(track->routeMode);
    wayptContent->clear();
    for(Waypoint& wp : track->waypoints) {
      if(!wp.name.empty() || showAllWaypts || istrack)
        addWaypointItem(wp);
    }
    updateDistances();
    // for now we'll try to make routes and tracks mutually exclusive
    routeModeBtn->setVisible(!istrack);
    routePluginBtn->setVisible(!istrack);
  }

  app->showPanel(trackPanel, true);
  static_cast<TextLabel*>(trackPanel->selectFirst(".panel-title"))->setText(track->title.c_str());
}

static std::string durationToStr(double totsecs)
{
  int hours = int(totsecs/3600);
  int mins = int((totsecs - hours*3600)/60);
  int secs = int(totsecs - hours*3600 - mins*60);
  return fstring("%02d:%02d:%02d", hours, mins, secs);  //fstring("%dh %02dm %02ds", hours, mins, secs);
}

static std::string speedToStr(double metersPerSec)
{
  double spd =  MapsApp::metricUnits ? metersPerSec*3.6 : metersPerSec*2.23694;
  return MapsApp::metricUnits ? fstring("%.*f km/h", spd < 10 ? 2 : 1, spd) : fstring("%.*f mph", spd < 10 ? 2 : 1, spd);
}

void MapsTracks::setStatsText(const char* selector, std::string str)
{
  Widget* widget = statsContent->selectFirst(selector);
  if(!widget || widget->node->type() != SvgNode::TEXT) return;
  char* value = const_cast<char*>(str.data());
  char* units = strchr(value, ' ');
  // we want to include space in units string
  static_cast<SvgTspan*>(widget->node->selectFirst(".stat-units-tspan"))->setText(units ? units : "");
  if(units) *units = '\0';
  static_cast<SvgTspan*>(widget->node->selectFirst(".stat-value-tspan"))->setText(value);
}

// deriv from 2nd order Savitky-Golay smoothing
static double calcCurrSlope(const std::vector<Waypoint>& locs, int nsteps = 101, double step = 1)
{
  if(locs.size() < 2) { return 0; }
  double d_target = locs.back().dist;
  size_t jj = locs.size() - 2;
  double result = 0;
  for(int kk = 0; kk < nsteps; ++kk) {
    while(jj > 0 && locs[jj].dist > d_target) { --jj; }
    double t = (d_target - locs[jj].dist)/(locs[jj+1].dist - locs[jj].dist);
    if(!(t >= 0)) { t = 0; }  // handle t < 0 and NaN
    double z_val = locs[jj].loc.alt + t*(locs[jj+1].loc.alt - locs[jj].loc.alt);
    result += ((nsteps - 1)/2 - kk) * z_val;
    d_target -= step;
  }
  double n = nsteps;
  return result/(step * n*(n*n - 1)/12);
}

void MapsTracks::updateStats(GpxFile* track)
{
  static const char* notime = u8"\u2014";  // emdash
  static std::vector<Waypoint> nolocs;
  std::vector<Waypoint>& locs = track->activeWay() ? track->activeWay()->pts : nolocs;
  bool isRecording = recordTrack && track == &recordedTrack;
  bool isTrack = track->routes.empty() && !track->tracks.empty();
  double totalTime = locs.empty() ? 0 : locs.back().loc.time - locs.front().loc.time;
  if(isRecording)
    totalTime += (mSecSinceEpoch() - lastTrackPtTime)/1000.0;

  double movingTime = isTrack ? 0 : totalTime;
  double trackDist = 0, trackAscent = 0, trackDescent = 0, ascentTime = 0, descentTime = 0;
  double estSpeed = 0, maxSpeed = 0, currSlope = 0, movingTimeGps = 0, rawDist = 0, movingDist = 0;
  size_t prevDistLoc = 0, prevVertLoc = 0;
  if(!locs.empty()) locs.front().dist = 0;
  for(size_t ii = 1; ii < locs.size(); ++ii) {
    Location& loc = locs[ii].loc;
    double dt = loc.time - locs[ii-1].loc.time;
    double dist = 1000*lngLatDist(loc.lngLat(), locs[prevDistLoc].lngLat());  //prev.lngLat()
    double disterr = loc.poserr + locs[prevDistLoc].loc.poserr;
    double vert = loc.alt - locs[prevVertLoc].loc.alt;
    double verterr = loc.alterr + locs[prevVertLoc].loc.alterr;

    if(loc.spd > 0.1f)
      movingTimeGps += dt;
    //if(dist > minTrackDist || vert > minTrackDist) {  // dt < minTrackTime

    rawDist += 1000*lngLatDist(loc.lngLat(), locs[ii-1].lngLat());
    disterr = disterr > 0 ? disterr : 10;
    if(!isTrack)
      trackDist = rawDist;
    else if(dist > disterr/2 || ii == locs.size() - 1) {  // be more generous with distance than vert
      double tdist = loc.time - locs[prevDistLoc].loc.time;
      trackDist += dist;
      movingTime += std::min(10.0, tdist);

      if(!track->hasSpeed) {
        double a = std::exp(-dt*speedInvTau);
        estSpeed = a*estSpeed + (1-a)*dist/tdist;
      }
      //double dd = dist/(ii - prevDistLoc);  -- this isn't really correct
      //for(++prevDistLoc; prevDistLoc < ii; ++prevDistLoc)
      //  locs[prevDistLoc].dist = locs[prevDistLoc-1].dist + dd;
      prevDistLoc = ii;
    }
    locs[ii].dist = trackDist;  //if(!origLocs.empty())
    if(!track->hasSpeed)
      loc.spd = estSpeed;
    maxSpeed = std::max(double(loc.spd), maxSpeed);

    verterr = verterr > 0 ? verterr : 20;
    if(std::abs(vert) > verterr) {
      double vertdt = dt;  //loc.time - locs[prevVertLoc].loc.time;
      for(size_t jj = prevVertLoc; jj < ii; ++jj) {
        // idea here is to try to exclude flat sections
        if(std::abs(loc.alt - locs[jj+1].loc.alt) <= verterr) {
          vertdt = loc.time - locs[jj].loc.time;
          break;
        }
      }
      trackAscent += std::max(0.0, vert);
      trackDescent += std::min(0.0, vert);
      ascentTime += vert > 0 ? vertdt : 0;
      descentTime += vert < 0 ? vertdt : 0;
      prevVertLoc = ii;
    }
  }

  //if(!locs.empty()) {
  //  for(size_t ii = locs.size() - 1; ii-- > 0;) {
  //    if(locs.back().dist - locs[ii].dist > 10 && (locs.back().loc.time - locs[ii].loc.time > 10
  //        || std::abs(locs.back().loc.alt - locs[ii].loc.alt) > 10)) {
  //      currSlope = (locs.back().loc.alt - locs[ii].loc.alt)/(locs.back().dist - locs[ii].dist);
  //      break;
  //    }
  //  }
  //}
  currSlope = calcCurrSlope(locs);

  liveStatsRow->setVisible(isRecording);
  nonliveStatsRow->setVisible(!isRecording);

  bool hasTime = !locs.empty() && locs.front().loc.time > 0;  // && istrack?
  statsContent->selectFirst(".track-start-date")->setText(
      hasTime ? ftimestr("%F %H:%M:%S", locs.front().loc.time*1000).c_str() : notime);
  statsContent->selectFirst(".track-end-date")->setText(
      hasTime ? ftimestr("%F %H:%M:%S", locs.back().loc.time*1000).c_str() : notime);

  setStatsText(".track-altitude", app->elevToStr(app->currLocation.alt));

  // m/s -> kph or mph
  setStatsText(".track-speed", speedToStr(currSpeed));

  double slopeDeg = std::atan(currSlope)*180/M_PI;
  setStatsText(".track-slope", fstring("%.*f%% (%.*f\u00B0)",
      std::abs(currSlope) < 0.1 ? 1 : 0, currSlope*100, std::abs(slopeDeg) < 10 ? 1 : 0, slopeDeg));

  float dir = app->currLocation.dir;
  setStatsText(".track-direction", (dir >= 0 && dir <= 360) ? fstring("%.0f\u00B0", dir) : notime);

  auto timeStr = durationToStr(totalTime);
  sparkStats->selectFirst(".spark-time")->setText(timeStr.c_str());
  setStatsText(".track-time", timeStr);
  setStatsText(".track-moving-time", durationToStr(movingTime));

  std::string distStr = MapsApp::distKmToStr(trackDist/1000);
  sparkStats->selectFirst(".spark-dist")->setText(distStr.c_str());
  setStatsText(".track-dist", distStr);

  setStatsText(".track-avg-speed", movingTime > 0 ? speedToStr(trackDist/movingTime) : notime);

  std::string ascentStr = app->elevToStr(trackAscent);
  //sparkStats->selectFirst(".spark-ascent")->setText(("+" + ascentStr).c_str());
  setStatsText(".track-ascent", ascentStr);

  std::string ascentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackAscent/(ascentTime/3600))
      : fstring("%.0f ft/h", (trackAscent*3.28084)/(ascentTime/3600));
  setStatsText(".track-ascent-speed", ascentTime > 0 ? ascentSpdStr : notime);

  std::string descentStr = app->elevToStr(trackDescent);  //app->metricUnits ? fstring("%.0f m", trackDescent) : fstring("%.0f ft", trackDescent*3.28084);
  //sparkStats->selectFirst(".spark-descent")->setText(descentStr.c_str());
  setStatsText(".track-descent", descentStr);

  std::string descentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackDescent/(descentTime/3600))
      : fstring("%.0f ft/h", (trackDescent*3.28084)/(descentTime/3600));
  setStatsText(".track-descent-speed", descentTime > 0 ? descentSpdStr : notime);

  // more stats
  setStatsText(".track-raw-dist", MapsApp::distKmToStr(rawDist/1000));
  setStatsText(".track-moving-dist", MapsApp::distKmToStr(movingDist/1000));
  setStatsText(".track-moving-time-gps", movingTimeGps > 0 ? durationToStr(movingTimeGps) : notime);
  setStatsText(".track-max-speed", maxSpeed > 0 ? speedToStr(maxSpeed) : notime);

  trackSummary = (totalTime > 0 ? (timeStr + " | ") : "") + distStr;
  trackSpark->setTrack(locs);
  plotVsTimeBtn->setVisible(totalTime > 0);

  if(plotDirty && plotWidgets[0]->isVisible()) {
    plotDirty = false;
    // if zoomed and scrolled to end of plot, scroll to include possible new location points
    if(trackPlot->zoomScale > 1 && trackPlot->zoomOffset == trackPlot->minOffset)
      trackPlot->zoomOffset = -INFINITY;
    trackPlot->setTrack(locs, track->waypoints);
  }

  if(isRecording) {
    recordedTrack.desc = "Recording | " + trackSummary;
    // set timer so as to update time as close to second boundary as possible
    int dt = std::max((std::floor(totalTime) + 1 - totalTime)*1000, 10.0);
    recordTimer = app->gui->setTimer(dt, trackPanel, recordTimer, [this](){
      recordTimer = NULL;
      if(activeTrack == &recordedTrack || tracksPanel->isVisible())
        updateStats(&recordedTrack);
      return 0;
    });
  }
}

static std::string wayptDetailStr(double distm, double alt, const std::string& desc)
{
  std::string s = MapsApp::distKmToStr(distm/1000);
  if(alt > 0)
    s.append(" | ").append(MapsApp::elevToStr(alt));
  if(!desc.empty())
    s.append(u8" \u2022 ").append(desc);  //u8" \u2022 " -- filled circle
  return s;
}

Waypoint* MapsTracks::nearestRoutePt(const Waypoint& wpt)
{
  float dist = FLT_MAX;
  Waypoint* rtePt = NULL;
  if(activeTrack->routeMode == "direct") {
    for(Waypoint& p : activeTrack->routes.back().pts) {
      double d = lngLatDist(wpt.lngLat(), p.lngLat());
      if(d < dist) {
        dist = d;
        rtePt = &p;
      }
    }
    return rtePt;
  }

  auto r = MapProjection::lngLatToProjectedMeters(wpt.lngLat()) - routeOrigin;
  glm::dvec2 radius = {routeCollider.xpad, routeCollider.ypad};
  do {
    isect2d::AABB<glm::vec2> aabb(r.x - radius.x, r.y - radius.y, r.x + radius.x, r.y + radius.y);
    routeCollider.intersect(aabb, [&](auto& a, auto& b) {
      float d = glm::distance(a.getCentroid(), b.getCentroid());
      if(d < dist) {
        dist = d;
        rtePt = (Waypoint*)(b.m_userData);
      }
      return true;
    }, false);
    radius *= 2;
  } while(!rtePt && radius.x < routeCollider.res_x && radius.y < routeCollider.res_y);

  return rtePt;
}

void MapsTracks::updateDistances()
{
  if(!activeTrack || activeTrack->routes.empty()) return;
  auto& route = activeTrack->routes.back().pts;
  if(route.empty()) return;
  auto wayPtItems = wayptContent->select(".listitem");  //->content->containerNode()->children();

  if(activeTrack->routeMode != "direct") {
    double xmin = DBL_MAX, ymin = DBL_MAX, xmax = DBL_MIN, ymax = DBL_MIN;
    for(const Waypoint& rtept : route) {
      auto r = MapProjection::lngLatToProjectedMeters(rtept.lngLat());
      xmin = std::min(xmin, r.x);
      ymin = std::min(ymin, r.y);
      xmax = std::max(xmax, r.x);
      ymax = std::max(ymax, r.y);
    }

    routeCollider.clear();
    routeCollider.resize({64, 64}, {xmax-xmin, ymax-ymin});
    routeOrigin = {xmin, ymin};
    for(const Waypoint& rtept : route) {
      auto r = MapProjection::lngLatToProjectedMeters(rtept.lngLat()) - routeOrigin;
      isect2d::AABB<glm::vec2> aabb(r.x, r.y, r.x, r.y);
      aabb.m_userData = (void*)(&rtept);
      routeCollider.insert(aabb);
    }
  }

  for(Widget* item : wayPtItems) {
    auto it = activeTrack->findWaypoint(item->node->getStringAttr("__sortkey"));
    Waypoint* rtePt = nearestRoutePt(*it);
    if(!rtePt) continue;  // should never happen
    double distm = rtePt->dist + 1000*lngLatDist(rtePt->lngLat(), it->lngLat());
    double alt = it->loc.alt > 0 ? it->loc.alt : rtePt->loc.alt;
    TextLabel* detail = static_cast<TextLabel*>(item->selectFirst(".detail-text"));
    detail->setText(wayptDetailStr(distm, alt, it->desc).c_str());
    it->dist = rtePt->dist;  // for stats plot
  }
}

void MapsTracks::createRoute(GpxFile* track)
{
  if(track->routes.empty() && !track->tracks.empty()) { return; } // don't add route if track already present
  if(track->routeMode == "draw") { return; }  // don't clear drawn route!
  retryBtn->setVisible(false);
  track->routes.clear();
  track->modified = true;
  plotDirty = true;
  if(track->routeMode == "direct") {
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
  plotDirty = true;
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
  // retry btn is on route edit toolbar!
  if(!routeEditTb->isVisible())
    routeEditBtn->onClicked();
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
  if(track->waypoints.empty()) {
    track->wayPtSerial = 0;  // we'll have to remove this if we add undo/redo functionality
    hideDirectRoutePreview();  //app->crossHair->setRoutePreviewOrigin();
  }
  if(routed)
    createRoute(track);
  updateWayptCount(wayptTabLabel, track);
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
    it->name = trimStr(titleEdit->text());
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
  TextBox* noteText = new TextBox(loadSVGFragment(
      R"(<text class="note-text weak" box-anchor="left" margin="0 10" font-size="12"></text>)"));
  noteText->setText(wpt.desc.c_str());
  noteText->setText(SvgPainter::breakText(static_cast<SvgText*>(noteText->node), app->getPanelWidth() - 20).c_str());

  Button* chooseListBtn = createListItem(MapsApp::uiIcon("waypoint"), track->title.c_str());
  chooseListBtn->selectFirst(".listitem-separator")->setVisible(false);
  //Button* chooseListBtn = createToolbutton(MapsApp::uiIcon("waypoint"), track->title.c_str(), true);
  Button* removeBtn = createToolbutton(MapsApp::uiIcon("discard"), "Delete");
  Button* addNoteBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit");

  chooseListBtn->onClicked = [=](){ populateTrack(track); };

  removeBtn->onClicked = [=](){
    removeWaypoint(track, uid);
    app->infoContent->selectFirst(".waypt-section")->setVisible(false);
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
    if(autoInsertWaypt) { insertWayptBtn->onClicked(); }
    insertionWpt = uid;
  });

  overflowMenu->addItem("Add waypoints after", [=](){
    if(track != activeTrack)
      populateTrack(track);
    if(autoInsertWaypt) { insertWayptBtn->onClicked(); }
    auto it = track->findWaypoint(uid);
    if(it != track->waypoints.end()) { ++it; }
    insertionWpt = it != track->waypoints.end() ? it->uid : "";
  });

  Widget* toolRow = createRow({chooseListBtn, removeBtn, addNoteBtn, overflowBtn});
  Widget* section = createColumn({toolRow, noteText}, "", "", "hfill");
  Widget* container = app->infoContent->selectFirst(".waypt-section");
  container->addWidget(section);
  container->setVisible(true);
}

Waypoint* MapsTracks::addWaypoint(Waypoint wpt)
{
  // prevent insertion of duplicate waypoint
  if(!activeTrack->waypoints.empty()) {
    auto it0 = insertionWpt.empty() ? activeTrack->waypoints.end() : activeTrack->findWaypoint(insertionWpt);
    if(it0 != activeTrack->waypoints.end() && it0->lngLat() == wpt.lngLat()) { return NULL; }
    if(it0 != activeTrack->waypoints.begin() && (--it0)->lngLat() == wpt.lngLat()) { return NULL; }
  }

  if(autoInsertWaypt) {
    Waypoint* rtept = nearestRoutePt(wpt);
    float mindist = FLT_MAX;
    for(const Waypoint& p : activeTrack->waypoints) {
      if(p.routed && p.dist > rtept->dist && p.dist < mindist) {
        mindist = p.dist;
        insertionWpt = p.uid;
      }
    }
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
  updateWayptCount(wayptTabLabel, activeTrack);
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
    activeTrack->modified = true;
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
  std::string desc = wayptDetailStr(wp.dist, wp.loc.alt, wp.desc);
  Button* item = createListItem(MapsApp::uiIcon("waypoint"), wpname.c_str(), desc.c_str());
  Widget* container = item->selectFirst(".child-container");

  item->onClicked = [this, uid](){
    //if(activeTrack == &navRoute && navRoute.routeMode != "direct") {
    //  replaceWaypt = true;
    //  insertionWpt = uid;
    //  app->showPanel(app->mapsSearch->searchPanel, true);
    //}
    //else {
      auto it = activeTrack->findWaypoint(uid);
      bool wasTapToAdd = std::exchange(tapToAddWaypt, false);
      app->setPickResult(it->lngLat(), it->name, it->props);
      tapToAddWaypt = wasTapToAdd;
      setPlaceInfoSection(activeTrack, *it);
    //}
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

bool MapsTracks::onFeaturePicked(const Tangram::FeaturePickResult* result)
{
  double id;
  if(result->properties->getNumber("track_feature_id", id)) {
    if(activeTrack && activeTrack->marker && activeTrack->marker->featureId == id && trackPanel->isVisible() &&
        !trackSliders->editMode && plotWidgets[0]->isVisible() && !std::isnan(app->tapLocation.longitude)) {
      // find the track point closest to chosen position; must be within 10 pixels
      double mindist = MapProjection::metersPerPixelAtZoom(app->map->getZoom())*10/1000.0;  // meters to km
      const Waypoint* closestWpt = NULL;
      for(const Waypoint& pt : activeTrack->activeWay()->pts) {
        double dist = lngLatDist(pt.lngLat(), app->tapLocation);
        if(dist > mindist) continue;
        mindist = dist;
        closestWpt = &pt;
      }
      if(closestWpt) {
        trackSliders->trackSlider->setVisible(true);
        trackSliders->trackSlider->setValue(trackPlot->plotVsDist ? closestWpt->dist/trackPlot->maxDist
            : closestWpt->loc.time - trackPlot->minTime/(trackPlot->maxTime - trackPlot->minTime));
        return true;
      }
    }
    for(GpxFile& track : tracks) {
      if(track.visible && &track != activeTrack && track.marker->featureId == id) {
        populateTrack(&track);
        return true;
      }
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
      LngLat mapcenter = app->getMapCenter();
      Point scrpos, scrcenter;
      app->map->lngLatToScreenPosition(mapcenter.longitude, mapcenter.latitude, &scrcenter.x, &scrcenter.y);
      app->map->lngLatToScreenPosition(mappos.longitude, mappos.latitude, &scrpos.x, &scrpos.y);
      app->crossHair->setRoutePreviewOrigin((scrpos - scrcenter)/MapsApp::gui->paintScale);
      double distkm = lngLatDist(mappos, app->getMapCenter());
      double distm = 1000*distkm;
      std::string mainstr = MapsApp::distKmToStr(distkm);
      // elevation (if available)
      double dz = 0, jut = 0;
      if(it->loc.alt != 0) {
        double elev = app->getElevation(app->getMapCenter());
        if(elev != 0) {
          dz = elev - it->loc.alt;
          mainstr += (dz > 0 ? " | +" : " | ") + MapsApp::elevToStr(dz);
          jut = dz*dz/std::sqrt(distm*distm + dz*dz);
        }
      }
      // bearing and change in bearing from prev segment
      double bearing = 180*lngLatBearing(mappos, app->getMapCenter())/M_PI;
      std::string detailstr = fstring("%.1f\u00B0", bearing < 0 ? bearing + 360 : bearing);
      if(it != activeTrack->waypoints.begin() && distkm > 0) {
        LngLat prevpt = (--it)->lngLat();
        double bchange = bearing - 180*lngLatBearing(prevpt, mappos)/M_PI;
        if(bchange > 180) bchange -= 360;
        else if(bchange < -180) bchange += 360;
        detailstr += fstring(" (%+.1f\u00B0)", bchange);
      }
      if(dz != 0 && distm > 0) {
        detailstr += fstring(" | %.1f%%", 100*dz/distm);
      }
      // don't show jut for multi-step measurements
      if(activeTrack->waypoints.size() < 2 && jut > 100) {
        detailstr += " | Jut " + MapsApp::elevToStr(jut);
      }
      previewDistText->setVisible(true);
      previewDistText->selectFirst(".main-text")->setText(mainstr.c_str());
      previewDistText->selectFirst(".detail-text")->setText(detailstr.c_str());
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
    else {
      if(!activeTrack || !findPickedWaypoint(activeTrack)) {  // navRoute is not in tracks
        for(GpxFile& track : tracks) {
          if(!track.visible || &track == activeTrack) continue;
          if(findPickedWaypoint(&track)) break;
          //if(track.marker->onPicked(app->pickedMarkerId)) {
          //  populateTrack(&track);
          //  break;
          //}
        }
      }
    }
  }
  else if(event == SUSPEND) {
    saveListOrder(app->config["tracks"]["list_order"], tracksContent->getOrder());
    if(activeTrack && activeTrack->modified)
      activeTrack->modified = !saveTrack(activeTrack);
  }
  else if(event == RESUME) {
    if(recordedTrack.visible || activeTrack == &recordedTrack)
      updateTrackMarker(&recordedTrack);  // rebuild marker
    if(activeTrack == &recordedTrack || (!activeTrack && tracksPanel->isVisible()))
      updateStats(&recordedTrack);
  }
}

void MapsTracks::setRouteMode(const std::string& mode)
{
  auto parts = splitStr<std::vector>(mode.c_str(), '-');
  const char* icon = "segment";  // mode == "direct"
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
    hideDirectRoutePreview();  //app->crossHair->setRoutePreviewOrigin();
  app->drawOnMap = routeEditTb->isVisible() && mode == "draw";
  if(!activeTrack || activeTrack->routeMode == mode) return;
  activeTrack->routeMode = mode;
  app->config["tracks"]["routemode"] = mode;
  createRoute(activeTrack);
}

void MapsTracks::hideDirectRoutePreview()
{
  previewDistText->setVisible(false);
  app->crossHair->setRoutePreviewOrigin();
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

void MapsTracks::newRoute(std::string mode)
{
  bool measure = mode == "measure";
  std::string dst = app->pickResultName.empty() ? lngLatToStr(app->pickResultCoord) : app->pickResultName;
  Waypoint wp1(app->currLocation.lngLat(), "Start");
  Waypoint wp2(app->pickResultCoord, dst, "", app->pickResultProps);
  double km = lngLatDist(wp1.lngLat(), wp2.lngLat());
  if(mode.empty()) {
    const char* dfltmode = (km < 10 ? "walk" : km < 100 ? "bike" : "drive");
    mode = app->cfg()["tracks"]["routemode"].as<std::string>(dfltmode);
  }
  navRoute = GpxFile();  //removeTrackMarkers(&navRoute);
  navRoute.title = measure ? "Measurement" : "Navigation";
  navRoute.routeMode = measure ? "direct" : mode;
  // clear insert waypoint mode for new route
  if(autoInsertWaypt) { insertWayptBtn->onClicked(); }
  // in some cases, we might want back btn to return to place info panel from measure/navigate; this would,
  //  not work currently if user opened place info from measure/navigate; also could make history too deep!
  app->showPanel(tracksPanel);
  app->panelToSkip = tracksPanel;
  populateTrack(&navRoute);
  toggleRouteEdit(measure);  // should we also show route edit for navigation?
  if(!measure && app->hasLocation && km > 0.01)
    addWaypoint(wp1);  //"Current location"
  addWaypoint(wp2);
}

void MapsTracks::addPlaceActions(Toolbar* tb)
{
  if(activeTrack) {
    Button* addWptBtn = createActionbutton(MapsApp::uiIcon("waypoint"), "Add waypoint", true);
    addWptBtn->onClicked = [=](){
      std::string nm = app->currLocPlaceInfo ? "Location at " + ftimestr("%H:%M:%S %F") : app->pickResultName;
      Waypoint* wpt = addWaypoint(app->pickResultCoord == trackHoverLoc.lngLat() ?
          trackHoverLoc : Waypoint{app->pickResultCoord, nm, "", app->pickResultProps});
      if(wpt)
        setPlaceInfoSection(activeTrack, *wpt);
    };

    Menu* addWptMenu = createMenu(Menu::VERT, false);
    addWptMenu->addItem("Prepend", [=](){
      auto prev = std::exchange(insertionWpt, activeTrack->waypoints.front().uid);
      addWptBtn->onClicked();
      insertionWpt = prev;
    });
    addWptMenu->addItem("Auto insert", [=](){
      auto prev = std::exchange(autoInsertWaypt, true);
      addWptBtn->onClicked();
      autoInsertWaypt = prev;
    });
    addWptMenu->addItem("Append", [=](){
      auto prev = std::exchange(insertionWpt, "");
      addWptBtn->onClicked();
      insertionWpt = prev;
    });
//#if PLATFORM_DESKTOP  // autoclose menu doesn't work w/ touch inside ScrollWidget!
//    addWptMenu->autoClose = true;
//    addWptBtn->setMenu(addWptMenu);
//#endif
    setupLongPressMenu(addWptBtn, addWptMenu);
    tb->addWidget(addWptBtn);
  }
  else {
    Button* routeBtn = createActionbutton(MapsApp::uiIcon("directions"), "Route", true);
    routeBtn->onClicked = [this](){ newRoute(""); };
    Menu* routeBtnMenu = createMenu(Menu::VERT);
    routeBtnMenu->addItem("Walk", MapsApp::uiIcon("walk"), [=](){ newRoute("walk"); });
    routeBtnMenu->addItem("Cycle", MapsApp::uiIcon("bike"), [=](){ newRoute("bike"); });
    routeBtnMenu->addItem("Drive", MapsApp::uiIcon("car"), [=](){ newRoute("drive"); });
//#if PLATFORM_DESKTOP  // autoclose menu doesn't work w/ touch inside ScrollWidget!
//    routeBtnMenu->autoClose = true;
//    routeBtn->setMenu(routeBtnMenu);
//#endif
    setupLongPressMenu(routeBtn, routeBtnMenu);
    tb->addWidget(routeBtn);

    Button* measureBtn = createActionbutton(MapsApp::uiIcon("measure"), "Measure", true);
    measureBtn->onClicked = [this](){ newRoute("measure"); };
    tb->addWidget(measureBtn);
  }
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
  if(jj == 0) return locs[0];
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
    FSPath file(MapsApp::baseDir, "tracks/" + toValidFilename(track->title) + ".gpx");
    std::string basepath = file.basePath();
    for(int ii = 1; file.exists(); ++ii)
      file = basepath + fstring(" (%d).gpx", ii);
    track->filename = file.path;
  }
  std::string relpath = FSPath(track->filename).relativeTo(MapsApp::baseDir);
  if(track->rowid < 0) {
    SQLiteStmt(app->bkmkDB, "INSERT INTO tracks (title, style, filename) VALUES (?,?,?);")
        .bind(track->title, track->style, relpath).exec();
    track->rowid = sqlite3_last_insert_rowid(app->bkmkDB);
  }
  else
    SQLiteStmt(app->bkmkDB, "UPDATE tracks SET title = ?, style = ?, filename = ? WHERE rowid = ?;")
        .bind(track->title, track->style, relpath, track->rowid).exec();
  track->loaded = true;
  tracksDirty = true;
}

void MapsTracks::loadTrackGPX(std::unique_ptr<PlatformFile> file)
{
  GpxFile track("", "", file->fsPath());  // set filename as fallback for track title
  loadGPX(&track, file->readAll().data());
  if(track.waypoints.empty() && !track.activeWay()) {
    MapsApp::messageBox("Import error", fstring("Error reading %s", file->fsPath().c_str()), {"OK"});
    return;
  }
  track.filename.clear();  // we will copy to tracks/ folder

  auto saveFn = [this](GpxFile&& gpx) {
    tracks.push_back(std::move(gpx));
    updateDB(&tracks.back());
    populateTrack(&tracks.back());
    saveTrack(&tracks.back());  // save track after updating description with stats
  };

  int ntracks = track.routes.size() + track.tracks.size();
  if(ntracks > 1) {
    int idx = 0;
    for(auto& rte : track.routes) {
      auto title = track.title + " Route " + std::to_string(++idx);
      if(!rte.title.empty()) { title = rte.title + " (" + title + ")"; }
      GpxFile subtrack(title, !rte.desc.empty() ? rte.desc : track.desc, "");
      subtrack.loaded = true;
      subtrack.hasSpeed = track.hasSpeed;
      subtrack.waypoints = track.waypoints;
      subtrack.routes.push_back(rte);
      saveFn(std::move(subtrack));
    }
    idx = 0;
    for(auto& trk : track.tracks) {
      auto title = track.title + " Track " + std::to_string(++idx);
      if(!trk.title.empty()) { title = trk.title + " (" + title + ")"; }
      GpxFile subtrack(title, !trk.desc.empty() ? trk.desc : track.desc, "");
      subtrack.loaded = true;
      subtrack.hasSpeed = track.hasSpeed;
      subtrack.waypoints = track.waypoints;
      subtrack.tracks.push_back(trk);
      saveFn(std::move(subtrack));
    }
    app->popPanel();  // show the track list
    MapsApp::messageBox("Import GPX", fstring("Imported %d tracks", ntracks), {"OK"});
  }
  else
    saveFn(std::move(track));
}

void MapsTracks::startRecording()
{
  recordTrack = true;
  app->setServiceState(true, gpsSamplePeriod, 0);  //minTrackTime*1.1, minTrackDist*1.1);
  std::string timestr = ftimestr("%FT%H.%M.%S");
  FSPath gpxPath(app->baseDir, "tracks/" + timestr + ".gpx");
  recordedTrack = GpxFile(timestr, "Recording", gpxPath.path);
  recordedTrack.loaded = true;
  recordedTrack.modified = true;  // ensure save after adding first point
  recordedTrack.tracks.emplace_back();
  //if(app->hasLocation)
  //  recordedTrack.tracks.back().pts.push_back(app->currLocation);
  recordedTrack.style = "#FF6030";  // use color in marker style instead?
  recordedTrack.marker = std::make_unique<TrackMarker>();  //app->map.get(), "layers.recording-track.draw.track");
  recordedTrack.marker->setProperties({{{"recording", 1}}});
  tracksDirty = true;
  lastTrackPtTime = mSecSinceEpoch();
  // create GPX file (so track data is safely saved w/o additional user input after tapping record btn)
  updateDB(&recordedTrack);
  //saveTrack(&recordedTrack);
  setTrackVisible(&recordedTrack, true);
  populateTrack(&recordedTrack);
  setTrackWidgets(TRACK_STATS);  // plot isn't very useful until there are enough points
  //editTrackContent->setVisible(true);
  tracksBtn->setIcon(MapsApp::uiIcon("track-recording"));
  recordTrackBtn->setChecked(true);
  app->config["tracks"]["recording"] = recordedTrack.rowid;
}

void MapsTracks::setTrackEdit(bool show)
{
  if(show == editTrackTb->isVisible()) return;
  if(show && (!plotWidgets[0]->isVisible() || recordTrack)) return;
  plotEditBtn->setChecked(show);
  editTrackTb->setVisible(show);
  trackSliders->setEditMode(show);
  app->map->markerSetVisible(trackHoverMarker, !show);
  if(show)
    trackSliders->setCropHandles(0, 1, SliderHandle::FORCE_UPDATE);
  else {
    app->map->markerSetVisible(trackStartMarker, false);
    app->map->markerSetVisible(trackEndMarker, false);
    if(!origLocs.empty()) {
      activeTrack->activeWay()->pts = std::move(origLocs);
      updateTrackMarker(activeTrack);  // rebuild marker
      plotDirty = true;
      trackPlot->zoomScale = 1.0;
      updateStats(activeTrack);
    }
  }
}

// currently, we need to create separate widgets for stats and waypoints panels
void MapsTracks::createEditDialog()
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
    auto title = trimStr(editTrackTitle->text());
    bool sametitle = title == activeTrack->title;
    // add entry for original track if making copy
    if(activeTrack->rowid >= 0 && savecopy) {
      tracks.emplace_back(activeTrack->title, activeTrack->desc, activeTrack->filename,
          activeTrack->style, activeTrack->rowid, activeTrack->archived);
    }
    activeTrack->title = title;
    activeTrack->style = colorToStr(editTrackColor->color());
    //activeTrack->desc = splitStr<std::vector>(activeTrack->desc + " ", " ").front() + trackSummary;
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
      activeTrack->modified = !saveTrack(activeTrack);
      origLocs.clear();
    }
    setTrackEdit(false);
  };
  //Widget* saveTrackContent = createInlineDialog(
  //    {editTrackRow}, "Save", [=](){ saveTrackFn(false); }, [=](){ setTrackEdit(false); });

  editTrackDialog.reset(createInputDialog(
      {editTrackRow}, "Edit Track", "Save", [=](){ saveTrackFn(false); }, [=](){ setTrackEdit(false); }));

  Button* saveBtn = static_cast<Button*>(editTrackDialog->selectFirst(".accept-btn"));
  saveBtn->setIcon(uiIcon("save"));
  Button* saveCopyBtn = createToolbutton(uiIcon("save-copy"), "Save copy", true);
  saveCopyBtn->onClicked = [=](){ editTrackDialog->finish(Dialog::CANCELLED); saveTrackFn(true); };
  saveBtn->parent()->containerNode()->addChild(saveCopyBtn->node, saveBtn->node);

  editTrackTitle->onChanged = [=](const char* s){ saveBtn->setEnabled(s[0]); saveCopyBtn->setEnabled(s[0]); };
  editTrackDialog->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      editTrackTitle->setText(activeTrack->title.c_str());
      editTrackColor->setColor(parseColor(activeTrack->style, Color::BLUE));
      //editTrackCopyCb->setChecked(false);
      saveCopyBtn->setVisible(activeTrack != &recordedTrack && activeTrack->rowid >= 0);
      if(!plotWidgets[0]->isVisible())  // keyboard would hide the plot edit toolbar
        gui->setFocused(editTrackTitle, SvgGui::REASON_TAB);
    }
    return false;
  });
}

static Widget* createStatsRow(std::vector<const char*> items)  // const char* title1, const char* class1, const char* title2, const char* class2)
{
  static const char* statBlockProtoSVG = R"(
    <g layout="box" box-anchor="hfill" margin="2 5">
      <!-- rect fill="none" width="150" height="40"/ -->
      <text class="title-text weak" box-anchor="left top" font-size="12"></text>
      <text class="stat-text" box-anchor="left top" margin="15 0 0 6">
        <tspan class="stat-value-tspan" font-size="16"></tspan>
        <tspan class="stat-units-tspan" font-size="13"></tspan>
      </text>
    </g>
  )";
  static std::unique_ptr<SvgNode> statBlockProto;
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

void MapsTracks::createStatsContent()
{
  liveStatsRow = createStatsRow({"Altitude", "track-altitude", "Speed", "track-speed",
      "Slope", "track-slope", "Direction", "track-direction"});
  nonliveStatsRow = createStatsRow({"Start", "track-start-date", "End", "track-end-date"});
  statsContent = createColumn({ liveStatsRow, nonliveStatsRow,
      createStatsRow({"Total time", "track-time", "Moving time", "track-moving-time",
                      "Distance", "track-dist", "Moving speed", "track-avg-speed"}),
      createStatsRow({"Ascent", "track-ascent", "Ascent speed", "track-ascent-speed",
                      "Descent", "track-descent", "Descent speed", "track-descent-speed"}) }, "", "", "hfill");
#ifndef NDEBUG
  statsContent->addWidget(createStatsRow({"Moving time (GPS)", "track-moving-time-gps", "Max speed", "track-max-speed",
      "Raw distance", "track-raw-dist", "Moving distance", "track-moving-dist"}));
#endif
  auto* statsContainer = createScrollWidget(statsContent);
  trackContainer->addWidget(statsContainer);
  statsWidgets.push_back(statsContainer);
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

  plotAltiBtn->onClicked = [=](){
    plotAltiBtn->setChecked(!plotAltiBtn->isChecked());
    trackPlot->plotAlt = plotAltiBtn->isChecked();
    if(!trackPlot->plotAlt && !trackPlot->plotSpd) {
      trackPlot->plotSpd = true;
      plotSpeedBtn->setChecked(true);
    }
    vertAxisSelBtn->setTitle(trackPlot->plotAlt && trackPlot->plotSpd ?
        "Altitude, Speed" : (trackPlot->plotAlt ? "Altitude" : "Speed"));
    trackPlot->redraw();
  };

  plotSpeedBtn->onClicked = [=](){
    plotSpeedBtn->setChecked(!plotSpeedBtn->isChecked());
    trackPlot->plotSpd = plotSpeedBtn->isChecked();
    if(!trackPlot->plotAlt && !trackPlot->plotSpd) {
      trackPlot->plotAlt = true;
      plotAltiBtn->setChecked(true);
    }
    vertAxisSelBtn->setTitle(trackPlot->plotAlt && trackPlot->plotSpd ?
        "Altitude, Speed" : (trackPlot->plotAlt ? "Altitude" : "Speed"));
    trackPlot->redraw();
  };

  auto horzAxisSelFn = [=](bool vsdist){
    trackPlot->plotVsDist = vsdist;
    plotVsDistBtn->setChecked(vsdist);
    plotVsTimeBtn->setChecked(!vsdist);
    horzAxisSelBtn->setTitle(trackPlot->plotVsDist ? "Distance" : "Time");
    trackPlot->redraw();
  };
  plotVsTimeBtn->onClicked = [=](){ horzAxisSelFn(false); };
  plotVsDistBtn->onClicked = [=](){ horzAxisSelFn(true); };

  plotEditBtn = createToolbutton(uiIcon("edit"), "Edit Track");
  plotEditBtn->onClicked = [=](){
    //if(activeTrack != &recordedTrack || recordTrack)
    bool edit = !editTrackTb->isVisible();
    setTrackEdit(edit);
  };

  Toolbar* axisSelRow = createToolbar(
      {vertAxisSelBtn, new TextBox(createTextNode("vs.")), horzAxisSelBtn, createStretch(), plotEditBtn});

  trackPlot = new TrackPlot();
  //trackPlot->node->setAttribute("box-anchor", "fill");
  trackPlot->node->addClass("scrollwidget-var-height");  // MapsApp::setWindowLayout() will set hfill or fill
  // trackSliders is container for trackPlot and slider handles
  trackSliders = createTrackSliders(trackPlot, 17, 21);
  trackPlot->sliders = trackSliders;

  auto cropSliderChanged = [=](){
    Waypoint startloc = interpTrack(activeTrack->activeWay()->pts, std::min(cropStart, cropEnd));
    if(trackStartMarker == 0) {
      trackStartMarker = app->map->markerAdd();
      app->map->markerSetPoint(trackStartMarker, startloc.lngLat());  // must set geometry before properties
      app->map->markerSetProperties(trackStartMarker, {{{"color", "#00C000"}}});
      app->map->markerSetStylingFromPath(trackStartMarker, "layers.track-marker.draw.marker");
    } else {
      app->map->markerSetVisible(trackStartMarker, true);
      app->map->markerSetPoint(trackStartMarker, startloc.lngLat());
    }
    Waypoint endloc = interpTrack(activeTrack->activeWay()->pts, std::max(cropStart, cropEnd));
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

  trackSliders->startSlider->onValueChanged = [=](real val){
    cropStart = trackPlot->plotPosToTrackPos(val);
    cropSliderChanged();
  };
  trackSliders->endSlider->onValueChanged = [=](real val){
    cropEnd = trackPlot->plotPosToTrackPos(val);
    cropSliderChanged();
  };

  Button* savePlotEdit = createToolbutton(MapsApp::uiIcon("save"), "Save...", true);
  savePlotEdit->onClicked = [=](){ showModalCentered(editTrackDialog.get(), app->gui); };

  Button* cancelPlotEdit = createToolbutton(MapsApp::uiIcon("cancel"), "Cancel");
  cancelPlotEdit->onClicked = [=](){ setTrackEdit(false); };

  Button* cropTrackBtn = createToolbutton(MapsApp::uiIcon("crop-outer"), "Crop", true);
  cropTrackBtn->onClicked = [=](){
    if(origLocs.empty()) origLocs = activeTrack->activeWay()->pts;
    auto& locs = activeTrack->activeWay()->pts;
    std::vector<Waypoint> newlocs;
    size_t startidx, endidx;
    newlocs.push_back(interpTrack(locs, std::min(cropStart, cropEnd), &startidx));
    auto endloc = interpTrack(locs, std::max(cropStart, cropEnd), &endidx);
    newlocs.insert(newlocs.end(), locs.begin() + startidx, locs.begin() + endidx);
    newlocs.push_back(endloc);
    locs.swap(newlocs);
    plotDirty = true;
    trackPlot->zoomScale = 1.0;
    cropStart = 0;  cropEnd = 1;
    //trackSliders->setCropHandles(0, 1, SliderHandle::FORCE_UPDATE);
    updateTrackMarker(activeTrack);  // rebuild marker
    updateStats(activeTrack);
  };

  Button* deleteSegmentBtn = createToolbutton(MapsApp::uiIcon("crop-inner"), "Excise", true);
  deleteSegmentBtn->onClicked = [=](){
    if(origLocs.empty()) origLocs = activeTrack->activeWay()->pts;
    auto& locs = activeTrack->activeWay()->pts;
    std::vector<Waypoint> newlocs;
    size_t startidx, endidx;
    if(std::min(cropStart, cropEnd) > 0) {
      auto startloc = interpTrack(locs, std::min(cropStart, cropEnd), &startidx);
      newlocs.insert(newlocs.end(), locs.begin(), locs.begin() + startidx);
      newlocs.push_back(startloc);
    }
    if(std::max(cropStart, cropEnd) < 1) {
      auto endloc = interpTrack(locs, std::max(cropStart, cropEnd), &endidx);
      newlocs.push_back(endloc);
      newlocs.insert(newlocs.end(), locs.begin() + endidx, locs.end());
    }
    locs.swap(newlocs);
    plotDirty = true;
    trackPlot->zoomScale = 1.0;
    cropStart = 0; cropEnd = 1;
    //trackSliders->setCropHandles(0, 1, SliderHandle::FORCE_UPDATE);
    updateTrackMarker(activeTrack);  // rebuild marker
    updateStats(activeTrack);
  };

  Button* moreTrackOptionsBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More options");
  Menu* trackPlotOverflow = createMenu(Menu::VERT_LEFT);
  moreTrackOptionsBtn->setMenu(trackPlotOverflow);

  trackPlotOverflow->addItem("Append", MapsApp::uiIcon("add-track"), [=](){
    if(!selectTrackDialog) {
      selectTrackDialog.reset(createSelectDialog("Choose Track", MapsApp::uiIcon("track")));
      selectTrackDialog->onSelected = [this](int idx){
        auto order = tracksContent->getOrder();
        int rowid = std::atoi(order[idx].c_str());
        auto it = std::find_if(tracks.rbegin(), tracks.rend(), [=](const GpxFile& a){ return a.rowid == rowid; });
        GpxWay* way = it != tracks.rend() ? it->activeWay() : NULL;
        if(!way) return;
        if(origLocs.empty()) origLocs = activeTrack->activeWay()->pts;
        auto& locs = activeTrack->activeWay()->pts;
        locs.insert(locs.end(), way->pts.begin(), way->pts.end());
        updateTrackMarker(activeTrack);  // rebuild marker
        plotDirty = true;
        trackPlot->zoomScale = 1.0;
        updateStats(activeTrack);
      };
    }

    auto order = tracksContent->getOrder();
    std::vector<std::string> items;
    items.reserve(order.size());
    for(const std::string& idstr : order) {
      int rowid = std::atoi(idstr.c_str());
      auto it = std::find_if(tracks.rbegin(), tracks.rend(), [=](const GpxFile& a){ return a.rowid == rowid; });
      if(it == tracks.rend()) continue;  // shouldn't happen
      items.push_back(it->title);
    }
    selectTrackDialog->addItems(items);
    showModalCentered(selectTrackDialog.get(), MapsApp::gui);
  });

  trackPlotOverflow->addItem("Reverse", MapsApp::uiIcon("reverse-direction"), [this](){
    if(origLocs.empty()) origLocs = activeTrack->activeWay()->pts;
    auto& locs = activeTrack->activeWay()->pts;
    std::reverse(locs.begin(), locs.end());
    updateTrackMarker(activeTrack);  // rebuild marker
    plotDirty = true;
    trackPlot->zoomScale = 1.0;
    updateStats(activeTrack);
  });

  trackPlotOverflow->addItem("Set elevation", MapsApp::uiIcon("mountain"), [this](){
    if(nElevPending > 0) {
      MapsApp::messageBox("Set elevation", "Previous task still running, please wait and try again.",
          {"OK"}, [this](std::string) { nElevPending = 0; });  // prevent getting stuck w/ nElevPending > 0
      return;
    }
    if(origLocs.empty()) { origLocs = activeTrack->activeWay()->pts; }
    GpxFile* track = activeTrack;
    GpxWay* way = activeTrack->activeWay();
    auto& locs = way->pts;
    size_t nlocs = locs.size();
    nElevPending = nlocs;
    for(size_t ptidx = 0; ptidx < nlocs; ++ptidx) {
      app->getElevation(locs[ptidx].lngLat(), [=](double ele){
        if(activeTrack != track || track->activeWay() != way || way->pts.size() != nlocs) {
          LOGW("Set elevation task aborted due to change of track!");
          --nElevPending;
          return;
        }
        if(!std::isnan(ele)) { way->pts[ptidx].loc.alt = ele; }
        if(--nElevPending == 0) {
          plotDirty = true;
          trackPlot->zoomScale = 1.0;
          updateStats(activeTrack);
        }
      }, true);  // always call callback so nElevPending is updated
    }
  });

  editTrackTb = createToolbar({cancelPlotEdit, savePlotEdit, createStretch(), cropTrackBtn, deleteSegmentBtn, moreTrackOptionsBtn});
  editTrackTb->setVisible(false);

  trackSliders->trackSlider->onValueChanged = [=](real s){
    if(editTrackTb->isVisible()) return;  // disabled when editing
    if(s < 0 || s > 1 || !activeTrack || !activeTrack->activeWay() || activeTrack->activeWay()->pts.empty()) {
      app->map->markerSetVisible(trackHoverMarker, false);
      return;
    }
    sliderPos = trackPlot->plotPosToTrackPos(s);
    trackHoverLoc = interpTrack(activeTrack->activeWay()->pts, sliderPos);
    //trackPlot->sliderLabel = trackPlot->plotVsDist ? MapsApp::distKmToStr(trackHoverLoc.dist/1000)
    //    : durationToStr(trackHoverLoc.loc.time - trackPlot->minTime);
    trackPlot->sliderAlt = app->elevToStr(trackHoverLoc.loc.alt);
    trackPlot->sliderSpd = speedToStr(trackHoverLoc.loc.spd);
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
    real pos = trackPlot->trackPosToPlotPos(sliderPos);
    trackSliders->trackSlider->setValue(pos, SliderHandle::NO_UPDATE);
    trackSliders->setCropHandles(start, end, SliderHandle::NO_UPDATE);
  };

  // stack (invisible) sliders on top of plot
  auto* plotContainer = createColumn({axisSelRow, trackSliders, editTrackTb}, "", "", "fill");
  //auto* plotContainer = createScrollWidget(plotContent);
  trackContainer->addWidget(plotContainer);
  plotWidgets.push_back(plotContainer);

  plotContainer->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      if(plotDirty) {
        if(activeTrack->activeWay())
          trackPlot->setTrack(activeTrack->activeWay()->pts, activeTrack->waypoints);
        else
          trackPlot->setTrack({}, activeTrack->waypoints);
      }
      plotDirty = false;
      app->map->markerSetVisible(trackHoverMarker, trackSliders->trackSlider->isVisible());
    }
    else if(event->type == SvgGui::INVISIBLE) {
      app->map->markerSetVisible(trackHoverMarker, false);
    }
    return false;
  });
}

void MapsTracks::createWayptContent()
{
  static const char* previewDistSVG = R"(
    <g layout="box" box-anchor="vfill">
      <text class="main-text" box-anchor="left" margin="0 10 6 10"></text>
      <text class="detail-text weak" box-anchor="left bottom" margin="0 10 2 10" font-size="12"></text>
    </g>
  )";

  wayptContent = new DragDropList;

  saveRouteBtn = createToolbutton(MapsApp::uiIcon("save"), "Save");
  saveRouteBtn->onClicked = [=](){
    if(activeTrack == &navRoute) {
      auto& dest = navRoute.waypoints.back().name;
      tracks.push_back(std::move(navRoute));
      activeTrack = &tracks.back();
      activeTrack->title = dest.empty() ? "Route" : "Route to " + dest;
      //activeTrack->desc = ftimestr("%F") + trackSummary;
      activeTrack->filename = "";  //saveRouteFile->text();
      activeTrack->visible = true;
      updateDB(activeTrack);
      populateTrack(activeTrack);
    }
    else if(activeTrack->filename.empty())
      LOGE("Active track has no filename!");
    else
      activeTrack->modified = !saveTrack(activeTrack);
  };

  bool hasPlugins = !app->pluginManager->routeFns.empty();
  routePluginBtn = createToolbutton(MapsApp::uiIcon(hasPlugins ? "plugin" : "no-plugin"), "Plugin");
  routePluginBtn->setEnabled(hasPlugins);
  if(hasPlugins) {
    std::vector<std::string> pluginTitles;
    for(auto& fn : app->pluginManager->routeFns)
      pluginTitles.push_back(fn.title.c_str());
    // default to the last plugin (hack to pick valhalla)
    pluginFn = std::min(pluginTitles.size()-1, app->config["tracks"]["plugin"].as<size_t>(INT_MAX));
    Menu* routePluginMenu = createRadioMenu(pluginTitles, [=](size_t idx){
      app->config["tracks"]["plugin"] = pluginFn = idx;
      if(activeTrack && activeTrack->routeMode != "direct")
        createRoute(activeTrack);
    }, pluginFn);
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
    waypointsDirty = true;
    populateTrack(activeTrack);
  };
  showAllWptsBtn->setChecked(showAllWaypts);
  trackOverflow->addItem(showAllWptsBtn);
  wayptWidgets.push_back(showAllWptsBtn);

  // route edit toolbar - I think this will eventually be a floating toolbar
  previewDistText = new Widget(loadSVGFragment(previewDistSVG));  //setMargins(6, 10);

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
    if(!activeTrack->routes.empty())
      activeTrack->routes.pop_back();
    if(!activeTrack->routes.empty())
      updateStats(activeTrack);
    updateTrackMarker(activeTrack);
  });

  rteEditOverflow->addItem("Reverse route", [this](){
    std::reverse(activeTrack->waypoints.begin(), activeTrack->waypoints.end());
    waypointsDirty = true;
    populateTrack(activeTrack);
    createRoute(activeTrack);
  });

  Button* tapToAddWayptBtn = createCheckBoxMenuItem("Tap to add waypoint");
  tapToAddWayptBtn->onClicked = [=](){
    tapToAddWaypt = !tapToAddWaypt;
    tapToAddWayptBtn->setChecked(tapToAddWaypt);
  };
  tapToAddWayptBtn->setChecked(tapToAddWaypt);
  rteEditOverflow->addItem(tapToAddWayptBtn);

  insertWayptBtn = createCheckBoxMenuItem("Auto insert waypoint");
  insertWayptBtn->onClicked = [=](){
    autoInsertWaypt = !autoInsertWaypt;
    insertWayptBtn->setChecked(autoInsertWaypt);
    if(autoInsertWaypt) { insertionWpt.clear(); }
  };
  insertWayptBtn->setChecked(autoInsertWaypt);
  rteEditOverflow->addItem(insertWayptBtn);

  routeEditTb = createToolbar({ previewDistText, createStretch(),
      retryBtn, mapCenterWayptBtn, searchWayptBtn, bkmkWayptBtn, rteEditOverflowBtn });

  auto wayptContainer = createColumn({ routeEditTb, wayptContent }, "", "", "fill");
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
        populateTrack(activeTrack);
    }
    else if(event->type == SvgGui::INVISIBLE) {
      directRoutePreview = false;
      app->drawOnMap = false;
      app->crossHair->setVisible(false);
      hideDirectRoutePreview();  //app->crossHair->setRoutePreviewOrigin();
    }
    return false;
  });
}

void MapsTracks::createTrackPanel()
{
  trackToolbar = app->createPanelHeader(MapsApp::uiIcon("track"), "");
  trackOverflow = createMenu(Menu::VERT_LEFT);
  trackContainer = createColumn({}, "", "", "fill");

  createEditDialog();

  pauseRecordBtn = createToolbutton(MapsApp::uiIcon("pause"), "Pause");
  stopRecordBtn = createToolbutton(MapsApp::uiIcon("stop"), "Stop");
  trackToolbar->addWidget(pauseRecordBtn);
  trackToolbar->addWidget(stopRecordBtn);

  pauseRecordBtn->onClicked = [=](){
    recordTrack = !recordTrack;
    recordedTrack.desc = (recordTrack ? "Recording | " : "Paused | ") + trackSummary;
    tracksDirty = true;
    pauseRecordBtn->setChecked(!recordTrack);  // should actually toggle between play and pause icons
    if(!recordTrack && recordGPXStrm && recordGPXStrm->is_open())
      recordGPXStrm->flush();
    if(recordTrack && editTrackTb->isVisible())
      setTrackEdit(false);  // show/hide track editing controls
    app->setServiceState(recordTrack, gpsSamplePeriod, 0);  //minTrackTime*1.1, minTrackDist*1.1);
  };

  stopRecordBtn->onClicked = [=](){
    app->gui->removeTimer(recordTimer);
    recordTimer = NULL;
    recordedTrack.desc = ftimestr("%F") + " | " + trackSummary;
    recordedTrack.marker->setProperties({{{"recording", 0}}});
    recordedTrack.modified = !saveTrack(&recordedTrack);
    if(recordTrack)
      app->setServiceState(0);
    //updateDB(&recordedTrack);
    tracks.push_back(std::move(recordedTrack));
    recordedTrack = GpxFile();
    recordTrack = false;
    tracksDirty = true;
    pauseRecordBtn->setChecked(false);
    tracksBtn->setIcon(MapsApp::uiIcon("track"));
    recordTrackBtn->setChecked(false);
    app->config["tracks"].remove("recording");
    populateTrack(&tracks.back());
  };

  saveCurrLocBtn = trackOverflow->addItem("Save location", [=](){
    //std::string namestr = "Location at " + ftimestr("%H:%M:%S %F");
    std::string namestr = "Location at T+"
        + durationToStr(app->currLocation.time - activeTrack->activeWay()->pts.front().loc.time);
    Waypoint* wpt = addWaypoint({app->currLocation, namestr});
    editWaypoint(activeTrack, *wpt, [this](){ populateTrack(activeTrack); });
  });

  trackPanel = app->createMapPanel(trackToolbar, NULL, trackContainer);
  trackPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE || event->type == SvgGui::INVISIBLE) {
      if(statsWidgets[0]->isVisible()) { for(Widget* w : statsWidgets) w->sdlEvent(gui, event); }
      else if(plotWidgets[0]->isVisible()) { for(Widget* w : plotWidgets) w->sdlEvent(gui, event); }
      else if(wayptWidgets[0]->isVisible()) { for(Widget* w : wayptWidgets) w->sdlEvent(gui, event); }
      if(event->type == SvgGui::INVISIBLE) {
        //editTrackContent->setVisible(false);
        setTrackEdit(false);
      }
    }
    else if(event->type == MapsApp::PANEL_CLOSED)
      closeActiveTrack();
    return false;
  });

  // tab bar for switching between stats, plot, and waypoints -- margin="0 0 0 10"
  static const char* sparkStatsSVG = R"(
    <g layout="box" box-anchor="fill">
      <g layout="flex" flex-direction="column" box-anchor="vfill" font-size="13">
        <text class="spark-dist" box-anchor="left"></text>
        <text class="spark-time" box-anchor="left"></text>
      </g>
    </g>
  )";
  sparkStats = new Widget(loadSVGFragment(sparkStatsSVG));

  trackSpark = new TrackSparkline();
  trackSpark->mBounds = Rect::wh(200, 40);  //80);
  trackSpark->node->setAttribute("box-anchor", "hfill");
  trackSpark->setMargins(1, 1);

  wayptTabLabel = new Widget(loadSVGFragment("<g box-anchor='hfill' layout='box'><text font-size='13'></text></g>"));

  Button* statsTabBtn = createToolbutton(uiIcon("sigma"), "Statistics");
  statsTabBtn->node->setAttribute("box-anchor", "hfill");
  statsTabBtn->selectFirst(".toolbutton-content")->addWidget(sparkStats);
  statsTabBtn->node->addClass("checked");
  statsWidgets.push_back(statsTabBtn->selectFirst(".checkmark"));
  statsTabBtn->onClicked = [=](){ setTrackWidgets(TRACK_STATS); };

  Button* plotTabBtn = createToolbutton(NULL, "Plot");  //uiIcon("graph-line")  -- maximize space for plot
  plotTabBtn->node->setAttribute("box-anchor", "hfill");
  plotTabBtn->selectFirst(".toolbutton-content")->addWidget(trackSpark);
  plotTabBtn->node->addClass("checked");
  plotWidgets.push_back(plotTabBtn->selectFirst(".checkmark"));
  plotTabBtn->onClicked = [=](){ setTrackWidgets(TRACK_PLOT); };

  Button* wayptTabBtn = createToolbutton(uiIcon("waypoint"), "Waypoints");
  wayptTabBtn->node->setAttribute("box-anchor", "hfill");
  wayptTabBtn->selectFirst(".toolbutton-content")->addWidget(wayptTabLabel);
  wayptTabBtn->node->addClass("checked");
  wayptWidgets.push_back(wayptTabBtn->selectFirst(".checkmark"));
  wayptTabBtn->onClicked = [=](){ setTrackWidgets(TRACK_WAYPTS); };

  Widget* tabBarRow = createRow({statsTabBtn, plotTabBtn, wayptTabBtn});
  trackContainer->addWidget(tabBarRow);

  // create content for three views
  createStatsContent();
  createPlotContent();
  createWayptContent();
  setTrackWidgets(TRACK_PLOT);

  // bottom of menu ... need icon on edit since menu needs space for checkbox menu items
  Button* editItem = trackOverflow->addItem("Edit", uiIcon("edit"), [=](){
    //showInlineDialogModal(editTrackContent);
    showModalCentered(editTrackDialog.get(), app->gui);
  });
  static_cast<SvgUse*>(editItem->node->selectFirst("use"))->setViewport(Rect::wh(24, 24));

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
    tracks.emplace_back(trimStr(newTrackTitle->text()), "", "", colorToStr(newTrackColor->color()));
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
  auto loadTrackFn = [=](std::unique_ptr<PlatformFile> file){ loadTrackGPX(std::move(file)); };
  loadTrackBtn->onClicked = [=](){ MapsApp::openFileDialog({{"GPX files", "gpx"}}, loadTrackFn); };

  recordTrackBtn = createToolbutton(MapsApp::uiIcon("record"), "Record", true);
  recordTrackBtn->onClicked = [=](){
    if(!recordedTrack.tracks.empty())
      populateTrack(&recordedTrack);  // show stats panel for recordedTrack, incl pause and stop buttons
    else
      startRecording();
  };

  tracksContent = new DragDropList;  //createColumn();
  auto tracksTb = app->createPanelHeader(MapsApp::uiIcon("folder"), "Tracks");
  tracksTb->addWidget(recordTrackBtn);
  tracksTb->addWidget(drawTrackBtn);
  tracksTb->addWidget(loadTrackBtn);
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
      " notes TEXT, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)), archived INTEGER DEFAULT 0);");

  minTrackDist = app->config["tracks"]["min_distance"].as<double>(0.5);
  minTrackTime = app->config["tracks"]["min_time"].as<double>(5);
  gpsSamplePeriod = app->config["tracks"]["sample_period"].as<float>(0.1f);

  createTrackListPanel();
  createTrackPanel();

  // load tracks for quick menu and for visible tracks
  loadTracks(false);
  const YAML::Node& vistracks = app->config["tracks"]["visible"];
  for(const auto& node : vistracks) {
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
        int rowid = std::atoi(items[ii].c_str());  //stoi() can throw exception!
        auto it = std::find_if(tracks.rbegin(), tracks.rend(), [=](const GpxFile& a){ return a.rowid == rowid; });
        if(it == tracks.rend()) continue;  // shouldn't happen
        GpxFile* track = &(*it);
        Button* item = createCheckBoxMenuItem(track->title.c_str());
        tracksMenu->addItem(item);
        SvgPainter::elideText(static_cast<SvgText*>(item->selectFirst(".title")->node), uiWidth - 100);
        item->onClicked = [=](){ setTrackVisible(track, !track->visible); };
        item->setChecked(track->visible || track == activeTrack);
      }
    }
    return false;
  });

  tracksBtn = app->createPanelButton(MapsApp::uiIcon("track"), "Tracks", tracksPanel);
  tracksBtn->setMenu(tracksMenu);

  if(const auto& rectrack = app->cfg()["tracks"]["recording"]) {
    int recid = rectrack.as<int>(-1);
    auto it = std::find_if(tracks.begin(), tracks.end(), [&](const GpxFile& t){ return t.rowid == recid; });
    if(it == tracks.end())
      app->config["tracks"].remove("recording");
    else if(it->tracks.empty() || it->tracks.front().pts.empty()) {
      app->config["tracks"].remove("recording");
      // have to wait until window created
      MapsApp::messageBox("Restore track",
          fstring("Error restoring recorded track; please check %s", it->filename.c_str()), {"OK"});
    }
    else {
      recordedTrack = std::move(*it);
      tracks.erase(it);
      setTrackVisible(&recordedTrack, true);  // load GPX and show
      recordedTrack.modified = true;  // ensure save if recording resumed
      recordTrackBtn->setChecked(true);
      pauseRecordBtn->setChecked(true);
      tracksBtn->setIcon(MapsApp::uiIcon("track-recording"));
    }
  }

  return tracksBtn;
}
