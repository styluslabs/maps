#pragma once

#include "mapscomponent.h"

class Widget;
class Button;
class SvgNode;
class TrackPlot;
class SelectDialog;
class DragDropList;

class MapsTracks : public MapsComponent {
public:
  using MapsComponent::MapsComponent;
  Button* createPanel();
  void tapEvent(LngLat location);
  void updateLocation(const Location& loc);

  MarkerID trackHoverMarker = 0;
  MarkerID trackStartMarker = 0;
  MarkerID trackEndMarker = 0;

  //struct TrackLoc : public Location { double dist; };
  using TrackLoc = Location;

  struct Waypoint
  {
    Location loc;
    double dist;
    std::string name;
    std::string desc;
    MarkerID marker;
    bool visible;  // <extensions><sl:route visible="true" routed="true"/>
    bool routed;
  };

  struct Track {
    std::string title;
    std::string detail;
    std::string gpxFile;
    std::string style;
    MarkerID marker = 0;
    std::vector<TrackLoc> locs;

    std::vector<Waypoint> waypoints;
    std::vector<Waypoint> route;
    std::vector<Waypoint> track;

    int rowid = -1;
    bool visible = true;
    bool archived = false;
  };

  std::vector<Track> tracks;
  Track recordedTrack;
  Track drawnTrack;

  Widget* tracksContent = NULL;
  Widget* tracksPanel = NULL;
  Widget* archivedContent = NULL;
  Widget* archivedPanel = NULL;
  Widget* statsContent = NULL;
  Widget* statsPanel = NULL;
  DragDropList* wayptContent = NULL;
  Widget* wayptPanel = NULL;
  TrackPlot* trackPlot = NULL;
  Button* pauseRecordBtn = NULL;
  Button* stopRecordBtn = NULL;

  double speedInvTau = 0.5;
  double minTrackDist = 2;  // meters
  double minTrackTime = 5;  // seconds

  static Track loadGPX(const char* gpxfile);
  static bool saveGPX(Track* track);

private:
  void loadTracks(bool archived);
  void showTrack(Track* track);  //, const char* styling);
  void setTrackVisible(Track* track, bool visible);
  void populateTracks(bool archived);
  void populateStats(Track* track);
  Widget* createTrackEntry(Track* track);
  TrackLoc interpTrack(const std::vector<TrackLoc>& locs, double s, size_t* idxout = NULL);

  Track* activeTrack = NULL;
  std::vector<TrackLoc> origLocs;
  double cropStart = 0;
  double cropEnd = 1;
  double recordLastSave = 0;
  bool recordTrack = false;
  bool drawTrack = false;
  bool tracksDirty = true;
  std::unique_ptr<SvgNode> trackListProto;
  std::unique_ptr<SelectDialog> selectTrackDialog;
};
