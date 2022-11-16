#pragma once

#include "mapsapp.h"

class MapTracks : public MapsComponent {
public:
  void showGUI();
  void onClick(LngLat location);

  std::vector<MarkerID> trackMarkers;
  MarkerID trackHoverMarker = 0;

  struct PointMarker {
      MarkerID markerId;
      LngLat coordinates;
  };
  std::vector<PointMarker> point_markers;

  MarkerID polyline_marker = 0;
  std::vector<LngLat> polyline_marker_coordinates;

  // GPX tracks
  struct TrackPt {
    LngLat pos;
    double dist;
    double elev;
  };
  std::vector<TrackPt> activeTrack;

private:
  void addGPXPolyline(const char* gpxfile);
};
