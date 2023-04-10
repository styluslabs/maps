#pragma once

#include "mapscomponent.h"

class Widget;

class MapsTracks : public MapsComponent {
public:
  using MapsComponent::MapsComponent;
  //void showGUI();
  Widget* createPanel();
  void tapEvent(LngLat location);

  std::string gpxFile;
  std::vector<MarkerID> trackMarkers;
  MarkerID trackHoverMarker = 0;

  //struct PointMarker {
  //    MarkerID markerId;
  //    LngLat coordinates;
  //};
  //std::vector<PointMarker> point_markers;

  MarkerID drawnTrackMarker = 0;
  std::vector<LngLat> drawnTrack;

  // GPX tracks
  struct TrackPt {
    LngLat pos;
    double dist;
    double elev;
  };
  std::vector<TrackPt> activeTrack;

private:
  void addGPXPolyline(const char* gpxfile);

  bool drawTrack = false;
};
