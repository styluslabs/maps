#pragma once

#include "mapscomponent.h"

class Widget;
class SvgNode;
class TrackPlot;

class MapsTracks : public MapsComponent {
public:
  using MapsComponent::MapsComponent;
  //void showGUI();
  Widget* createPanel();
  void tapEvent(LngLat location);

  void updateLocation(const Location& _loc);

  std::string gpxFile;
  //std::vector<MarkerID> trackMarkers;
  MarkerID trackHoverMarker = 0;

  //struct PointMarker {
  //    MarkerID markerId;
  //    LngLat coordinates;
  //};
  //std::vector<PointMarker> point_markers;

  MarkerID drawnTrackMarker = 0;
  std::vector<LngLat> drawnTrack;

  struct TrackLoc : public Location {
    double dist;
  };

  struct Track {
    std::string title;
    std::string detail;
    std::string gpxFile;
    MarkerID marker;
    std::vector<TrackLoc> locs;
    int rowid;
  };

  std::vector<Track> tracks;
  Track recordedTrack;

  // GPX tracks
  //struct TrackPt {
  //  LngLat pos;
  //  double dist;
  //  double elev;
  //};
  //std::vector<TrackPt> activeTrack;

  Widget* tracksContent = NULL;
  Widget* tracksPanel = NULL;
  Widget* statsContent = NULL;
  Widget* statsPanel = NULL;
  TrackPlot* trackPlot = NULL;

  double speedInvTau = 0.5;
  double minTrackDist = 2;  // meters
  double minTrackTime = 5;  // seconds

private:
  Track loadGPX(const char* gpxfile);
  bool saveGPX(Track& track);
  void showTrack(Track& track);
  void populateTracks();
  void populateStats(Track& track);
  void createTrackEntry(Track& track);

  Track* activeTrack = NULL;
  double recordLastSave = 0;
  bool recordTrack = false;
  bool drawTrack = false;
  std::unique_ptr<SvgNode> trackListProto;
};
