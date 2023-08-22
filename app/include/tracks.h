#pragma once

#include "mapscomponent.h"

class Widget;
class Button;
class SvgNode;
class TrackPlot;
class SelectDialog;
class DragDropList;

struct Waypoint
{
  Location loc;
  double dist;
  std::string name;
  std::string desc;
  std::string uid;  // id for DragDropList
  MarkerID marker;
  bool visible;  // <extensions><sl:route visible="true" routed="true"/>
  bool routed;

  Waypoint(const Location& _loc, double _dist = 0, const std::string& _name = "", const std::string& _desc = "")
      : loc(_loc), dist(_dist), name(_name), desc(_desc) {}
  LngLat lngLat() const { return loc.lngLat(); }
};

struct Track {
  std::string title;
  std::string detail;
  std::string gpxFile;
  std::string style;
  MarkerID marker = 0;
  //std::vector<TrackLoc> locs;

  std::vector<Waypoint> waypoints;
  std::vector<Waypoint> route;
  std::vector<Waypoint> track;

  int rowid = -1;
  bool visible = true;
  bool archived = false;
};

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
  //using TrackLoc = Location;

  std::vector<Track> tracks;
  Track recordedTrack;
  Track drawnTrack;

  std::string routeMode = "direct";  // "walk", "bike", "drive"
  int pluginFn = 0;

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
  void updateTrackMarker(Track* track);
  void showTrack(Track* track, bool show);
  void setTrackVisible(Track* track, bool visible);
  void populateTracks(bool archived);
  void populateStats(Track* track);
  Widget* createTrackEntry(Track* track);
  Waypoint interpTrack(const std::vector<Waypoint>& locs, double s, size_t* idxout = NULL);

  Track* activeTrack = NULL;
  std::vector<Waypoint> origLocs;
  double cropStart = 0;
  double cropEnd = 1;
  double recordLastSave = 0;
  bool recordTrack = false;
  bool drawTrack = false;
  bool tracksDirty = true;
  std::unique_ptr<SvgNode> trackListProto;
  std::unique_ptr<SelectDialog> selectTrackDialog;
};
