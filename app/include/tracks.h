#pragma once

#include "mapscomponent.h"
#include "gpxfile.h"

class TrackPlot;
class TrackSparkline;
class TrackSliders;

class MapsTracks : public MapsComponent {
public:
  using MapsComponent::MapsComponent;
  Button* createPanel();
  void addPlaceActions(Toolbar* tb);
  //void tapEvent(LngLat location);
  void updateLocation(const Location& loc);
  void onMapEvent(MapEvent_t event);
  void addRoute(std::vector<Waypoint>&& route);
  void addRouteStep(const char* instr, int rteptidx);
  bool onPickResult();
  bool tapEvent(LngLat location);
  void fingerEvent(int action, LngLat pos);
  void routePluginError(const char* err);

  MarkerID trackHoverMarker = 0;
  MarkerID trackStartMarker = 0;
  MarkerID trackEndMarker = 0;

  std::vector<GpxFile> tracks;
  GpxFile recordedTrack;
  GpxFile navRoute;
  GpxFile* activeTrack = NULL;

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
  Button* saveRouteBtn = NULL;
  Button* routeModeBtn = NULL;
  Button* routePluginBtn = NULL;
  TextBox* previewDistText = NULL;
  Button* sparkStats = NULL;
  TrackSparkline* trackSpark = NULL;
  Button* retryBtn = NULL;
  TrackSliders* trackSliders = NULL;
  Button* routeEditBtn = NULL;
  Toolbar* routeEditTb = NULL;

  double speedInvTau = 0.5;
  double minTrackDist = 2;  // meters
  double minTrackTime = 5;  // seconds

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
  void updateStats(std::vector<Waypoint>& locs);
  void updateDistances();
  bool findPickedWaypoint(GpxFile* track);
  void toggleRouteEdit(bool show);
  Widget* createEditDialog(Button* editTrackBtn);
  void refreshWayptPlaceInfo(GpxFile* track, const Waypoint& wpt);

  int pluginFn = 0;
  std::vector<Waypoint> origLocs;
  std::string insertionWpt;
  Waypoint trackHoverLoc = LngLat{0, 0};
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
