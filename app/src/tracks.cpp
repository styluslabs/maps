#include "tracks.h"
#include "mapsapp.h"
#include "util.h"
#include "imgui.h"
#include "imgui_stl.h"

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_utils.hpp"

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
  using namespace rapidxml;  // https://rapidxml.sourceforge.net/manual.html
  file<> xmlFile(gpxfile); // Default template is char
  xml_document<> doc;
  doc.parse<0>(xmlFile.data());
  xml_node<>* trk = doc.first_node("gpx")->first_node("trk");
  if(!trk) logMsg("Error loading %s\n", gpxfile);
  activeTrack.clear();
  while(trk) {
    xml_node<>* trkseg = trk->first_node("trkseg");
    while(trkseg) {
      std::vector<LngLat> track;
      xml_node<>* trkpt = trkseg->first_node("trkpt");
      while(trkpt) {
        xml_attribute<>* lat = trkpt->first_attribute("lat");
        xml_attribute<>* lon = trkpt->first_attribute("lon");
        track.emplace_back(atof(lon->value()), atof(lat->value()));

        xml_node<>* ele = trkpt->first_node("ele");
        double dist = activeTrack.empty() ? 0 : activeTrack.back().dist + lngLatDist(activeTrack.back().pos, track.back());
        activeTrack.push_back({track.back(), dist, atof(ele->value())});

        trkpt = trkpt->next_sibling("trkpt");
      }
      if(!track.empty()) {
        MarkerID marker = app->map->markerAdd();
        app->map->markerSetStylingFromString(marker, polylineStyle.c_str());
        app->map->markerSetPolyline(marker, track.data(), track.size());
        trackMarkers.push_back(marker);
      }
      trkseg = trkseg->next_sibling("trkseg");
    }
    trk = trk->next_sibling("trk");
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
