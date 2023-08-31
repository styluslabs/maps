#pragma once

#include "mapscomponent.h"

class Widget;
class Button;
class SvgNode;
class TrackPlot;
class SelectDialog;
class DragDropList;
class Toolbar;

struct Waypoint
{
  Location loc;
  double dist = 0;
  std::string name;
  std::string desc;
  std::string uid;  // id for DragDropList
  MarkerID marker = 0;
  bool visible = true;  // <extensions><sl:route visible="true" routed="true"/>
  bool routed = true;

  Waypoint(const Location& _loc, const std::string& _name = "", const std::string& _desc = "")
      : loc(_loc), name(_name), desc(_desc) {}
  Waypoint(const LngLat& _r, const std::string& _name = "", const std::string& _desc = "")
      : loc({0, _r.latitude, _r.longitude, 0, 0, 0, 0, 0, 0, 0}), name(_name), desc(_desc) {}
  Waypoint(const Location& _loc, double _dist) : loc(_loc), dist(_dist) {}
  LngLat lngLat() const { return loc.lngLat(); }
};

struct GpxWay
{
  std::string title;
  std::string desc;
  std::vector<Waypoint> pts;

  GpxWay() {}
  GpxWay(const std::string& _title, const std::string& _desc) : title(_title), desc(_desc) {}
};

struct GpxFile {
  std::string title;
  std::string desc;
  std::string filename;
  std::string style;
  MarkerID marker = 0;

  std::vector<Waypoint> waypoints;
  std::vector<GpxWay> routes;
  std::vector<GpxWay> tracks;

  int rowid = -1;
  int wayPtSerial = 0;
  bool visible = false;
  bool archived = false;
  bool loaded = false;
  bool modified = false;

  GpxFile() {}
  GpxFile(const std::string& _title, const std::string& _desc, const std::string& _file)
      : title(_title), desc(_desc), filename(_file) {}

  GpxWay* activeWay() { return !routes.empty() ? &routes.front() : !tracks.empty()? &tracks.front() : NULL; }
  void addWaypoint(Waypoint wpt) { waypoints.push_back(wpt); waypoints.back().uid = std::to_string(++wayPtSerial); }
};

class MapsTracks : public MapsComponent {
public:
  using MapsComponent::MapsComponent;
  Button* createPanel();
  void addPlaceActions(Toolbar* tb);
  //void tapEvent(LngLat location);
  void updateLocation(const Location& loc);
  void onMapEvent(MapEvent_t event);
  void addRoute(std::vector<Waypoint>&& route);

  MarkerID trackHoverMarker = 0;
  MarkerID trackStartMarker = 0;
  MarkerID trackEndMarker = 0;
  MarkerID previewMarker = 0;

  std::vector<GpxFile> tracks;
  GpxFile recordedTrack;
  GpxFile navRoute;

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
  Button* routeModeBtn = NULL;

  double speedInvTau = 0.5;
  double minTrackDist = 2;  // meters
  double minTrackTime = 5;  // seconds

  static bool loadGPX(GpxFile* track, const char* gpxSrc = NULL);
  static bool saveGPX(GpxFile* track);

private:
  void loadTracks(bool archived);
  void updateTrackMarker(GpxFile* track);
  void showTrack(GpxFile* track, bool show);
  void setTrackVisible(GpxFile* track, bool visible);
  void populateTracks(bool archived);
  void populateStats(GpxFile* track);
  void populateWaypoints(GpxFile* track);
  Widget* createTrackEntry(GpxFile* track);
  Waypoint interpTrack(const std::vector<Waypoint>& locs, double s, size_t* idxout = NULL);
  void setRouteMode(const std::string& mode);
  void addWaypointItem(Waypoint& wp);
  void setPlaceInfoSection(const Waypoint& wpt);
  void createRoute(GpxFile* track);
  void removeWaypoint(const std::string& uid);
  void viewEntireTrack(GpxFile* track);

  std::string routeMode = "direct";  // "walk", "bike", "drive"
  int pluginFn = 0;
  GpxFile* activeTrack = NULL;
  std::vector<Waypoint> origLocs;
  std::vector<LngLat>  previewRoute;
  double cropStart = 0;
  double cropEnd = 1;
  double recordLastSave = 0;
  bool recordTrack = false;
  bool drawTrack = false;
  bool directRoutePreview = false;
  bool tracksDirty = true;
  bool waypointsDirty = true;
  bool showAllWaypts = false;
  bool archiveLoaded = false;
  std::unique_ptr<SelectDialog> selectTrackDialog;
};
