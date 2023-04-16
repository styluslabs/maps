#include "tracks.h"
#include "mapsapp.h"
#include "util.h"
//#include "imgui.h"
//#include "imgui_stl.h"

#include <ctime>
#include <iomanip>
#include "pugixml.hpp"
#include "sqlite3/sqlite3.h"

static bool markerUseStylingPath = true;
static std::string markerStylingPath = "layers.touch.point.draw.icons";
static std::string markerStylingString = R"RAW(
style: text
text_source: "function() { return 'MARKER'; }"
font:
    family: Open Sans
    size: 12px
    fill: white
)RAW";
static std::string polylineStyle = "{ style: lines, interactive: true, color: red, width: 4px, order: 5000 }";

//static bool add_point_marker_on_click = false;
//static bool add_polyline_marker_on_click = false;
//static bool point_markers_position_clipped = false;

// https://www.topografix.com/gpx_manual.asp
MapsTracks::Track MapsTracks::loadGPX(const char* gpxfile)
{
  Track track;
  double dist = 0;
  pugi::xml_document doc;
  doc.load_file(gpxfile);
  pugi::xml_node trk = doc.child("gpx").child("trk");
  if(!trk) logMsg("Error loading %s\n", gpxfile);
  track.title = trk.child("name").child_value();
  track.gpxFile = gpxfile;
  //activeTrack.clear();
  gpxFile = gpxfile;
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

/*
void MapsTracks::showGUI()
{
    static std::string gpxFile;

    Map* map = app->map;
    if (ImGui::CollapsingHeader("Markers")) {
        ImGui::Checkbox("Add point markers on click", &add_point_marker_on_click);
        if (ImGui::RadioButton("Use Styling Path", markerUseStylingPath)) { markerUseStylingPath = true; }
        if (markerUseStylingPath) {
            ImGui::InputText("Path", &markerStylingPath);
        }
        if (ImGui::RadioButton("Use Styling String", !markerUseStylingPath)) { markerUseStylingPath = false; }
        if (!markerUseStylingPath) {
            ImGui::InputTextMultiline("String", &markerStylingString);
        }
        if (ImGui::Button("Clear point markers")) {
            for (const auto marker : point_markers) {
                map->markerRemove(marker.markerId);
            }
            point_markers.clear();
        }

        ImGui::Checkbox("Add polyline marker points on click", &add_polyline_marker_on_click);
        if (ImGui::Button("Clear polyline marker")) {
            if (!polyline_marker_coordinates.empty()) {
                map->markerRemove(polyline_marker);
                polyline_marker_coordinates.clear();
            }
        }

        ImGui::Checkbox("Point markers use clipped position", &point_markers_position_clipped);
        if (point_markers_position_clipped) {
            // Move all point markers to "clipped" positions.
            for (const auto& marker : point_markers) {
                double screenClipped[2];
                map->lngLatToScreenPosition(marker.coordinates.longitude, marker.coordinates.latitude, &screenClipped[0], &screenClipped[1], true);
                LngLat lngLatClipped;
                map->screenPositionToLngLat(screenClipped[0], screenClipped[1], &lngLatClipped.longitude, &lngLatClipped.latitude);
                map->markerSetPoint(marker.markerId, lngLatClipped);
            }

            // Display coordinates for last marker.
            if (!point_markers.empty()) {
                auto& last_marker = point_markers.back();
                double screenPosition[2];
                map->lngLatToScreenPosition(last_marker.coordinates.longitude, last_marker.coordinates.latitude, &screenPosition[0], &screenPosition[1]);
                float screenPositionFloat[2] = {static_cast<float>(screenPosition[0]), static_cast<float>(screenPosition[1])};
                ImGui::InputFloat2("Last Marker Screen", screenPositionFloat, 5, ImGuiInputTextFlags_ReadOnly);
                double screenClipped[2];
                map->lngLatToScreenPosition(last_marker.coordinates.longitude, last_marker.coordinates.latitude, &screenClipped[0], &screenClipped[1], true);
                float screenClippedFloat[2] = {static_cast<float>(screenClipped[0]), static_cast<float>(screenClipped[1])};
                ImGui::InputFloat2("Last Marker Clipped", screenClippedFloat, 5, ImGuiInputTextFlags_ReadOnly);
            }
        }

        ImGui::InputText("GPX File", &gpxFile);
        if (ImGui::Button("Add")) {
          addGPXPolyline(gpxFile.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace")) {
          for (auto marker : trackMarkers)
            map->markerRemove(marker);
          addGPXPolyline(gpxFile.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All")) {
          activeTrack.clear();
          for (auto marker : trackMarkers)
            map->markerRemove(marker);
        }

        if(!activeTrack.empty()) {
          size_t N = 200;
          double dd = activeTrack.back().dist/N;
          double d = dd/2;
          std::vector<float> plot;
          plot.reserve(N);
          for(size_t ii = 0, jj = 0; ii < N; ++ii, d += dd) {
            while(activeTrack[jj].dist < d) ++jj;
            double f = (d - activeTrack[jj-1].dist)/(activeTrack[jj].dist - activeTrack[jj-1].dist);
            plot.push_back( f*activeTrack[jj].elev + (1-f)*activeTrack[jj-1].elev );
          }
          ImGui::TextUnformatted("Track elevation");
          ImGui::PlotLines("", plot.data(), plot.size(), 0, NULL, FLT_MAX, FLT_MAX, {0, 250});

          if(ImGui::IsItemHovered()) {
            double s = (ImGui::GetMousePos().x - ImGui::GetItemRectMin().x)/ImGui::GetItemRectSize().x;
            if(s > 0 && s < 1) {
              double sd = s*activeTrack.back().dist;

              size_t jj = 0;
              while(activeTrack[jj].dist < sd) ++jj;
              double f = (sd - activeTrack[jj-1].dist)/(activeTrack[jj].dist - activeTrack[jj-1].dist);
              double lat = f*activeTrack[jj].pos.latitude + (1-f)*activeTrack[jj-1].pos.latitude;
              double lon = f*activeTrack[jj].pos.longitude + (1-f)*activeTrack[jj-1].pos.longitude;
              if(trackHoverMarker == 0) {
                trackHoverMarker = map->markerAdd();
                //map->markerSetStylingFromPath(trackHoverMarker, markerStylingPath.c_str());
                map->markerSetStylingFromString(trackHoverMarker, markerStylingString.c_str());
              }
              map->markerSetVisible(trackHoverMarker, true);
              map->markerSetPoint(trackHoverMarker, LngLat(lon, lat));
              return;
            }
          }
        }
        if(trackHoverMarker > 0)
          map->markerSetVisible(trackHoverMarker, false);
    }
}
*/

void MapsTracks::tapEvent(LngLat location)
{
  if (!drawTrack)
    return;

  //if (add_point_marker_on_click) {
  //  auto marker = map->markerAdd();
  //  map->markerSetPoint(marker, location);
  //  if (markerUseStylingPath) {
  //    map->markerSetStylingFromPath(marker, markerStylingPath.c_str());
  //  } else {
  //    map->markerSetStylingFromString(marker, markerStylingString.c_str());
  //  }
  //
  //  point_markers.push_back({ marker, location });
  //}

  // clear track btn?  How to save drawn track?

  if (trackMarkers.empty()) {
    drawnTrack.clear();
    trackMarkers.push_back(app->map->markerAdd());
    app->map->markerSetStylingFromString(trackMarkers.back(), polylineStyle.c_str());
  }
  double dist = activeTrack.empty() ? 0 : activeTrack.back().dist + lngLatDist(activeTrack.back().pos, location);
  activeTrack.push_back({location, dist, 0});
  drawnTrack.push_back(location);
  app->map->markerSetPolyline(drawnTrackMarker, drawnTrack.data(), drawnTrack.size());
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
      if(activeTrack == &recordedTrack && !app->panelHistory.empty() && app->panelHistory.back() == statsPanel)
        populateStats(recordedTrack);
      if(recordedTrack.marker > 0)
        showTrack(recordedTrack);
      if(_loc.time > recordLastSave + 60) {
        saveGPX(recordedTrack);
        recordLastSave = _loc.time;
      }
    }
  }
}

void MapsTracks::showTrack(Track& track)
{
  std::vector<LngLat> pts;
  for(const TrackLoc& loc : track.locs)
    pts.push_back(loc.lngLat());
  // choose unused color automatically, but allow user to change color (line width, dash too?)
  if(track.marker <= 0)
    track.marker = app->map->markerAdd();
  app->map->markerSetStylingFromString(track.marker, polylineStyle.c_str());
  app->map->markerSetPolyline(track.marker, pts.data(), pts.size());
}

bool MapsTracks::saveGPX(Track& track)
{
  // saving track
  char timebuf[256];
  pugi::xml_document doc;
  pugi::xml_node trk = doc.append_child("gpx").append_child("trk");
  trk.append_child("name").append_child(pugi::node_pcdata).set_value(track.title.c_str());
  pugi::xml_node seg = trk.append_child("trkseg");
  for(const TrackLoc& loc : track.locs) {
    pugi::xml_node trkpt = seg.append_child("trkpt");
    trkpt.append_attribute("lng").set_value(loc.lng);
    trkpt.append_attribute("lat").set_value(loc.lat);
    trkpt.append_child("ele").append_child(pugi::node_pcdata).set_value(fstring("%.1f", loc.alt).c_str());
    time_t t = time_t(loc.time);
    strftime(timebuf, sizeof(timebuf), "%FT%TZ", gmtime(&t));
    trkpt.append_child("time").append_child(pugi::node_pcdata).set_value(timebuf);
    // should we save direction or speed? position, altitude error?
  }
  return doc.save_file(track.gpxFile.c_str(), "  ");
}

// New GUI

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
  void setTrack(MapsTracks::Track& track);

  std::function<void(real x)> onHovered;

  Path2D plot;
  Rect mBounds;
  double minAlt;
  double maxAlt;
  double maxDist;
  static Color bgColor;
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
    if(event->type == SDL_FINGERMOTION && !gui->pressedWidget)
      onHovered((event->tfinger.x - mBounds.left)/mBounds.width());
    else if(event->type == SvgGui::LEAVE)
      onHovered(-1);
    else
      return false;
    return true;
  });
}

void TrackPlot::setTrack(MapsTracks::Track& track)
{
  minAlt = FLT_MAX;
  maxAlt = 0;
  maxDist = track.locs.back().dist;
  plot.clear();
  for(auto& tpt : track.locs) {
    plot.addPoint(Point(tpt.dist, app->metricUnits ? tpt.alt : tpt.alt*3.28084));
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

  // vertical axis
  real labelw = 0;
  int nvert = 5;
  double dh = (maxAlt - minAlt)/nvert;
  for(int ii = 0; ii < nvert; ++ii)
    labelw = std::max(labelw, p->drawText(0, ii*h/nvert, fstring("%.0f", minAlt + ii*dh).c_str()));
  p->drawLine(Point(labelw + 5, 15), Point(labelw + 5, h));
  // horz axis
  int nhorz = 5;
  for(int ii = 0; ii < nhorz; ++ii)
    p->drawText(ii*w/nhorz, 0, fstring("%.0f", ii*maxDist).c_str());
  p->drawLine(Point(labelw + 5, 15), Point(w, 15));

  p->translate(-minAlt, 0);
  p->scale(w/maxDist, h/dh);
  p->translate(labelw + 10, 15);

  //drawCheckerboard(p, w, h, 4, 0x18000000);
  p->drawPath(plot);
}

//void MapsTracks::onSceneLoaded()
//{
//  trackHoverMarker = 0;
//  if(!gpxFile.empty())
//    addGPXPolyline(gpxFile.c_str());
//}

void MapsTracks::createTrackEntry(Track& track)
{
  Button* item = new Button(trackListProto->clone());

  item->onClicked = [&](){
    if(track.locs.empty() && !track.gpxFile.empty())
      track.locs = std::move(loadGPX(track.gpxFile.c_str()).locs);
    populateStats(track);
  };

  Button* showBtn = new Button(item->containerNode()->selectFirst(".visibility-btn"));
  showBtn->onClicked = [&](){
    if(track.marker > 0) {
      app->map->markerRemove(track.marker);
      track.marker = 0;
    }
    else {
      if(track.locs.empty() && !track.gpxFile.empty())
        track.locs = std::move(loadGPX(track.gpxFile.c_str()).locs);
      showTrack(track);
    }
  };

  Button* delBtn;
  delBtn->onClicked = [&](){
    DB_exec(app->bkmkDB, "DELETE FROM tracks WHERE rowid = ?", NULL, [=](sqlite3_stmt* stmt){
      sqlite3_bind_int(stmt, 1, track.rowid);
    });
    populateTracks();
  };

  // track detail: date? duration? distance?
  item->selectFirst(".title-text")->setText(track.title.c_str());
  item->selectFirst(".detail-text")->setText(track.detail.c_str());
  tracksContent->addWidget(item);
}

void MapsTracks::populateTracks()
{
  // separate fn to create list entry so we can easily include recordedTrack?
  // HEY, we don't want to load track points until needed!

  // where does track come from?
  // - we remember index into tracks vector
  // - we remember pointer to Track ... change tracks to vector of Track ptrs?
  // - we build Track on demand ... then where do we persist markerID?

  app->showPanel(tracksPanel);
  app->gui->deleteContents(tracksContent, ".listitem");
  tracks.clear();

  DB_exec(app->bkmkDB, "SELECT rowid, title, filename, strftime('%Y-%m-%d', timestamp, 'unixepoch') FROM tracks;", [this](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    std::string title = (const char*)(sqlite3_column_text(stmt, 1));
    std::string filename = (const char*)(sqlite3_column_text(stmt, 2));
    std::string date = (const char*)(sqlite3_column_text(stmt, 3));
    tracks.push_back({title, date, filename, 0, {}, rowid});
  });

  if(recordTrack)
    createTrackEntry(recordedTrack);
  for(Track& track : tracks)
    createTrackEntry(track);
}


void MapsTracks::populateStats(Track& track)
{
  app->showPanel(statsPanel);
  statsPanel->selectFirst(".panel-title")->setText(track.title.c_str());
  trackPlot->setTrack(track);
  activeTrack = &track;

  if(track.marker <= 0)
    showTrack(track);
  app->map->markerSetStylingFromString(track.marker, activeTrackStyle.c_str());

  // how to calculate max speed?
  double trackDist = 0, trackAscent = 0, trackDescent = 0, ascentTime = 0, descentTime = 0, movingTime = 0;
  double currSpeed = 0, maxSpeed = 0;
  Location prev = track.locs.front();
  for(size_t ii = 1; ii < track.locs.size(); ++ii) {
    const Location& loc = track.locs[ii];
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

  Widget* trackStatsPanel = statsContent;

  const Location& loc = track.locs.back();
  std::string posStr = fstring("%.6f, %.6f", loc.lat, loc.lng);
  trackStatsPanel->selectFirst(".track-position")->setText(posStr.c_str());

  std::string altStr = app->metricUnits ? fstring("%.0f m", loc.alt) : fstring("%.0f ft", loc.alt*3.28084);
  trackStatsPanel->selectFirst(".track-altitude")->setText(altStr.c_str());

  // m/s -> kph or mph
  std::string speedStr = app->metricUnits ? fstring("%.2f km/h", currSpeed*3.6) : fstring("%.2f mph", currSpeed*2.23694);
  trackStatsPanel->selectFirst(".track-speed")->setText(speedStr.c_str());

  double ttot = track.locs.back().time - track.locs.front().time;
  int hours = int(ttot/3600);
  int mins = int((ttot - hours*3600)/60);
  int secs = int(ttot - hours*3600 - mins*60);
  trackStatsPanel->selectFirst(".track-time")->setText(fstring("%dh %dm %ds", hours, mins, secs).c_str());

  ttot = movingTime;
  hours = int(ttot/3600);
  mins = int((ttot - hours*3600)/60);
  secs = int(ttot - hours*3600 - mins*60);
  trackStatsPanel->selectFirst(".track-moving-time")->setText(fstring("%dh %dm %ds", hours, mins, secs).c_str());

  double distUser = app->metricUnits ? trackDist/1000 : trackDist*0.000621371;
  std::string distStr = fstring(app->metricUnits ? "%.2f km" : "%.2f mi", distUser);
  trackStatsPanel->selectFirst(".track-dist")->setText(distStr.c_str());

  std::string avgSpeedStr = fstring(app->metricUnits ? "%.2f km/h" : "%.2f mph", distUser/(movingTime/3600));
  trackStatsPanel->selectFirst(".track-avg-speed")->setText(avgSpeedStr.c_str());

  std::string ascentStr = app->metricUnits ? fstring("%.0f m", trackAscent) : fstring("%.0f ft", trackAscent*3.28084);
  trackStatsPanel->selectFirst(".track-ascent")->setText(ascentStr.c_str());

  std::string ascentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackAscent/(ascentTime/3600))
      : fstring("%.0f ft/h", (trackAscent*3.28084)/(ascentTime/3600));
  trackStatsPanel->selectFirst(".track-ascent-speed")->setText(ascentSpdStr.c_str());

  std::string descentStr = app->metricUnits ? fstring("%.0f m", trackDescent) : fstring("%.0f ft", trackDescent*3.28084);
  trackStatsPanel->selectFirst(".track-descent")->setText(descentStr.c_str());

  std::string descentSpdStr = app->metricUnits ? fstring("%.0f m/h", trackDescent/(descentTime/3600))
      : fstring("%.0f ft/h", (trackDescent*3.28084)/(descentTime/3600));
  trackStatsPanel->selectFirst(".track-descent-speed")->setText(descentSpdStr.c_str());
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
// NEXT: maybe work on bookmarks to help inform data model
// - very unhappy w/ data model; some options:
//  - only save rowid and load GPX on demand? we can keep track in activeTrack until replaced
//  - if we are going to persist TrackLocs, we should persist through updating track list
// - add track to DB when done recording
// - option to always record track (i.e. save location history) (while app is in foreground)?

// draw track: we could show distance (length)
// - aside: would it be easier to draw track by moving map and taping button to drop waypoint at map center?
// - we also want option to trace track by dragging finger

// how to handle scene reload? options:
// - modify tangram to preserve markers when scene reloaded
// - add wrapper fn to MapsApp to automatically handle restoring markers
// - use a ClientDataSource instead of Markers, which can be easily added back to new scene
// ... we can modify ClientDataSource to support removal of individual features
// I think any of these options would be better than manually restoring markers in every MapComponent

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

      </g>
    </g>
  )";
  trackListProto.reset(loadSVGFragment(trackListProtoSVG));

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
    if(saveGPX(recordedTrack)) {
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

  trackPlot->onHovered = [this](real s){
    if(s < 0 || s > 1 || !activeTrack) {
      app->map->markerSetVisible(trackHoverMarker, false);
      return;
    }
    const std::vector<TrackLoc>& locs = activeTrack->locs;
    double sd = s*locs.back().dist;
    size_t jj = 0;
    while(locs[jj].dist < sd) ++jj;
    double f = (sd - locs[jj-1].dist)/(locs[jj].dist - locs[jj-1].dist);
    double lat = f*locs[jj].lat + (1-f)*locs[jj-1].lat;
    double lon = f*locs[jj].lng + (1-f)*locs[jj-1].lng;
    if(trackHoverMarker == 0) {
      trackHoverMarker = app->map->markerAdd();
      //map->markerSetStylingFromPath(trackHoverMarker, markerStylingPath.c_str());
      app->map->markerSetStylingFromString(trackHoverMarker, markerStylingString.c_str());
    }
    app->map->markerSetVisible(trackHoverMarker, true);
    app->map->markerSetPoint(trackHoverMarker, LngLat(lon, lat));
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
      recordedTrack = Track{timestr, "", std::string(timestr) + ".gpx", 0, {}, -1};
      //tracks.insert(tracks.begin(), {"Title", "default filename", 0, {}});
      saveTrackContent->setVisible(true);
      populateTracks();
      populateStats(recordedTrack);
    }
    else {
      // create DB entry for track
      populateTracks();
      recordedTrack = Track{};
    }
  };

  TextEdit* gpxPath = createTextEdit();
  tracksContent->addWidget(createTitledRow("GPX File", gpxPath));

  Button* addGpxBtn = createPushbutton("Add");

  addGpxBtn->onClicked = [=](){
    auto track = loadGPX(gpxPath->text().c_str());
    if(!track.locs.empty()) {

      const char* query = "INSERT INTO tracks (osm_id,filename) VALUES (?,?);";
      DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
        sqlite3_bind_text(stmt, 1, osm_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, list, -1, SQLITE_STATIC);
      });

      //tracks.push_back(std::move(track));
      populateTracks();

      showTrack(tracks.back());
      populateStats(tracks.back());
      //trackPlot->setTrack(activeTrack);
      loadTrackPanel->setVisible(false);
    }
  };

  Widget* btnRow = createRow();
  btnRow->addWidget(addGpxBtn);
  //btnRow->addWidget(replaceGpxBtn);
  loadTrackPanel->addWidget(btnRow);
  loadTrackPanel->setVisible(false);
  tracksContent->addWidget(loadTrackPanel);

  auto tracksTb = createToolbar();
  tracksTb->addWidget(app->createHeaderTitle(SvgGui::useFile(":/icons/ic_menu_select_path.svg"), "Tracks"));
  tracksTb->addWidget(createStretch());
  tracksTb->addWidget(drawTrackBtn);
  tracksTb->addWidget(loadTrackBtn);
  tracksTb->addWidget(recordTrackBtn);
  auto tracksHeader = app->createPanelHeader(tracksTb);
  tracksPanel = app->createMapPanel(tracksContent, tracksHeader);

  auto statsTb = createToolbar();

  statsTb->addWidget(app->createHeaderTitle(SvgGui::useFile(":/icons/ic_menu_bookmark.svg"), ""));
  statsTb->addWidget(createStretch());
  statsTb->addWidget(editTrackBtn);
  auto statsHeader = app->createPanelHeader(statsTb);
  statsPanel = app->createMapPanel(statsContent, statsHeader);


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

  Button* tracksBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_select_path.svg"), "Tracks");
  //tracksBtn->setMenu(sourcesMenu);
  tracksBtn->onClicked = [=](){
    populateTracks();
  };

  return tracksBtn;
}
