#include "tracks.h"
#include "mapsapp.h"
#include "util.h"
#include "imgui.h"
#include "imgui_stl.h"

//#include "rapidxml/rapidxml.hpp"
//#include "rapidxml/rapidxml_utils.hpp"
#include "pugixml.hpp"

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

static bool add_point_marker_on_click = false;
static bool add_polyline_marker_on_click = false;
static bool point_markers_position_clipped = false;

// https://www.topografix.com/gpx_manual.asp
void MapsTracks::addGPXPolyline(const char* gpxfile)
{
  //using namespace rapidxml;  // https://rapidxml.sourceforge.net/manual.html
  pugi::xml_document doc;
  doc.load_file(gpxfile);
  pugi::xml_node trk = doc.child("gpx").child("trk");
  if(!trk) logMsg("Error loading %s\n", gpxfile);
  activeTrack.clear();
  while(trk) {
    pugi::xml_node trkseg = trk.child("trkseg");
    while(trkseg) {
      std::vector<LngLat> track;
      pugi::xml_node trkpt = trkseg.child("trkpt");
      while(trkpt) {
        pugi::xml_attribute lat = trkpt.attribute("lat");
        pugi::xml_attribute lon = trkpt.attribute("lon");
        track.emplace_back(lon.as_double(), lat.as_double());

        pugi::xml_node ele = trkpt.child("ele");
        double dist = activeTrack.empty() ? 0 : activeTrack.back().dist + lngLatDist(activeTrack.back().pos, track.back());
        activeTrack.push_back({track.back(), dist, atof(ele.child_value())});

        trkpt = trkpt.next_sibling("trkpt");
      }
      if(!track.empty()) {
        MarkerID marker = app->map->markerAdd();
        app->map->markerSetStylingFromString(marker, polylineStyle.c_str());
        app->map->markerSetPolyline(marker, track.data(), track.size());
        trackMarkers.push_back(marker);
      }
      trkseg = trkseg.next_sibling("trkseg");
    }
    trk = trk.next_sibling("trk");
  }
}

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

void MapsTracks::tapEvent(LngLat location)
{
  Map* map = app->map;
  if (add_point_marker_on_click) {
    auto marker = map->markerAdd();
    map->markerSetPoint(marker, location);
    if (markerUseStylingPath) {
      map->markerSetStylingFromPath(marker, markerStylingPath.c_str());
    } else {
      map->markerSetStylingFromString(marker, markerStylingString.c_str());
    }

    point_markers.push_back({ marker, location });
  }

  if (add_polyline_marker_on_click) {
    if (polyline_marker_coordinates.empty()) {
      polyline_marker = map->markerAdd();
      map->markerSetStylingFromString(polyline_marker, polylineStyle.c_str());
    }
    polyline_marker_coordinates.push_back(location);
    map->markerSetPolyline(polyline_marker, polyline_marker_coordinates.data(), polyline_marker_coordinates.size());
  }
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
  void setTrack(const std::vector<MapsTracks::TrackPt>& track);

  std::function<void(real x)> onHovered;

  Path2D plot;
  Rect mBounds;
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

void TrackPlot::setTrack(const std::vector<MapsTracks::TrackPt>& track)
{
  plot.clear();
  for(auto& tpt : track)
    plot.addPoint(Point(tpt.dist, tpt.elev));
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
  //drawCheckerboard(p, w, h, 4, 0x18000000);
  p->drawPath(plot);
}


// list of existing tracks - combo or list box?
// - delete track btn
// new track/record
// - resolution / power use
// load GPX
// - file selection: use OS dialog for iOS, Android; what about desktop? Just text box for now? OS dialog?
// elevation plot

Widget* MapsTracks::createPanel()
{
  Widget* tracksContent = createColumn();

  TrackPlot* trackPlot = new TrackPlot();
  trackPlot->node->setAttribute("box-anchor", "fill");
  trackPlot->setMargins(1, 5);

#if PLATFORM_MOBILE
#else
  TextEdit* gpxPath = createTextEdit();
  tracksContent->addWidget(createTitledRow("GPX File", gpxPath));

  Button* addGpxBtn = createPushbutton("Add");
  Button* replaceGpxBtn = createPushbutton("Replace");
  Button* clearGpxBtn = createPushbutton("Clear All");

  addGpxBtn->onClicked = [=](){
    addGPXPolyline(gpxPath->text().c_str());
    trackPlot->setTrack(activeTrack);
  };
  replaceGpxBtn->onClicked = [=](){
    for (auto marker : trackMarkers)
      app->map->markerRemove(marker);
    addGPXPolyline(gpxPath->text().c_str());
    trackPlot->setTrack(activeTrack);
  };
  clearGpxBtn->onClicked = [=](){
    activeTrack.clear();
    for (auto marker : trackMarkers)
      app->map->markerRemove(marker);
  };
  Widget* btnRow = createRow();
  btnRow->addWidget(addGpxBtn);
  btnRow->addWidget(replaceGpxBtn);
  btnRow->addWidget(clearGpxBtn);
  tracksContent->addWidget(btnRow);
#endif
  tracksContent->addWidget(trackPlot);

  trackPlot->onHovered = [this](real s){
    if(s < 0 || s > 1) {
      app->map->markerSetVisible(trackHoverMarker, false);
      return;
    }
    double sd = s*activeTrack.back().dist;
    size_t jj = 0;
    while(activeTrack[jj].dist < sd) ++jj;
    double f = (sd - activeTrack[jj-1].dist)/(activeTrack[jj].dist - activeTrack[jj-1].dist);
    double lat = f*activeTrack[jj].pos.latitude + (1-f)*activeTrack[jj-1].pos.latitude;
    double lon = f*activeTrack[jj].pos.longitude + (1-f)*activeTrack[jj-1].pos.longitude;
    if(trackHoverMarker == 0) {
      trackHoverMarker = app->map->markerAdd();
      //map->markerSetStylingFromPath(trackHoverMarker, markerStylingPath.c_str());
      app->map->markerSetStylingFromString(trackHoverMarker, markerStylingString.c_str());
    }
    app->map->markerSetVisible(trackHoverMarker, true);
    app->map->markerSetPoint(trackHoverMarker, LngLat(lon, lat));
  };

  auto tracksTitle = app->createHeaderTitle(SvgGui::useFile(":/icons/ic_menu_select_path.svg"), "Tracks");
  auto tracksHeader = app->createPanelHeader(tracksTitle);
  Widget* tracksPanel = app->createMapPanel(tracksContent, tracksHeader);

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
    app->showPanel(tracksPanel);
  };

  return tracksBtn;
}
