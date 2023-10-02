#pragma once

#include "mapscomponent.h"

struct Waypoint
{
  Location loc;
  double dist = 0;
  std::string name;
  std::string desc;
  std::string uid;  // id for DragDropList
  MarkerID marker = 0;
  //bool visible = true;  // <extensions><sl:route visible="true" routed="true"/>
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

class TrackPlot;
class TrackSparkline;

struct GpxFile {
  std::string title;
  std::string desc;
  std::string filename;
  std::string style;
  std::string routeMode = "direct";  // "walk", "bike", "drive"
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

  std::vector<Waypoint>::iterator findWaypoint(const std::string& uid);
  std::vector<Waypoint>::iterator addWaypoint(Waypoint wpt, const std::string& nextuid = {});
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
  bool onPickResult();
  bool tapEvent(LngLat location);
  void routePluginError(const char* err);

  MarkerID trackHoverMarker = 0;
  MarkerID trackStartMarker = 0;
  MarkerID trackEndMarker = 0;
  MarkerID previewMarker = 0;

  std::vector<GpxFile> tracks;
  GpxFile recordedTrack;
  GpxFile navRoute;

  DragDropList* tracksContent = NULL;
  Widget* archivedContent = NULL;
  Widget* tracksPanel = NULL;
  Widget* archivedPanel = NULL;
  Widget* statsContent = NULL;
  Widget* statsPanel = NULL;
  DragDropList* wayptContent = NULL;
  Widget* wayptPanel = NULL;
  TrackPlot* trackPlot = NULL;
  Button* pauseRecordBtn = NULL;
  Button* stopRecordBtn = NULL;
  Button* routeModeBtn = NULL;
  TextBox* previewDistText = NULL;
  Button* sparkStats = NULL;
  TrackSparkline* trackSpark = NULL;
  Button* retryBtn = NULL;

  double speedInvTau = 0.5;
  double minTrackDist = 2;  // meters
  double minTrackTime = 5;  // seconds

  static bool loadGPX(GpxFile* track, const char* gpxSrc = NULL);
  static bool saveGPX(GpxFile* track);
  static std::vector<Waypoint> decodePolylineStr(const std::string& encoded, double precision = 1E6);

private:
  void loadTracks(bool archived);
  void updateTrackMarker(GpxFile* track);
  void showTrack(GpxFile* track, bool show);
  void setTrackVisible(GpxFile* track, bool visible);
  void populateArchived();
  void populateTracks();
  void populateStats(GpxFile* track);
  void populateWaypoints(GpxFile* track);
  Widget* createTrackEntry(GpxFile* track);
  Waypoint interpTrack(const std::vector<Waypoint>& locs, double s, size_t* idxout = NULL);
  void setRouteMode(const std::string& mode);
  void addWaypointItem(Waypoint& wp, const std::string& nextuid = {});
  void setPlaceInfoSection(GpxFile* track, const Waypoint& wpt);
  void createRoute(GpxFile* track);
  void removeWaypoint(GpxFile* track, const std::string& uid);
  void viewEntireTrack(GpxFile* track);
  void updateDB(GpxFile* track);
  Waypoint* addWaypoint(Waypoint wpt);
  void removeTrackMarkers(GpxFile* track);

  int pluginFn = 0;
  GpxFile* activeTrack = NULL;
  std::vector<Waypoint> origLocs;
  std::vector<LngLat> previewRoute;
  std::string insertionWpt;
  double cropStart = 0;
  double cropEnd = 1;
  double recordLastSave = 0;
  bool recordTrack = false;
  //bool drawTrack = false;
  bool directRoutePreview = false;
  bool tracksDirty = true;
  bool waypointsDirty = true;
  bool showAllWaypts = false;
  bool archiveLoaded = false;
  bool tapToAddWaypt = false;
  bool replaceWaypt = false;  // replacing waypt from search or bookmarks
  bool stealPickResult = false;  // adding waypt from search or bookmarks
  std::unique_ptr<SelectDialog> selectTrackDialog;
};
