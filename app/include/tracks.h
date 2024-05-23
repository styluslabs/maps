#pragma once

#include <list>
#include "mapscomponent.h"
#include "gpxfile.h"
#include "ulib/fileutil.h"

class TrackPlot;
class TrackSparkline;
class TrackSliders;
struct Timer;

class MapsTracks : public MapsComponent {
public:
  using MapsComponent::MapsComponent;
  Button* createPanel();
  void addPlaceActions(Toolbar* tb);
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

  std::list<GpxFile> tracks;
  GpxFile recordedTrack;
  GpxFile navRoute;
  GpxFile* activeTrack = NULL;

  double currSpeed = 0;
  double speedInvTau = 0.5;
  double minTrackDist = 2;  // meters
  double minTrackTime = 5;  // seconds
  float gpsSamplePeriod = 0.1f;  // seconds

private:
  void loadTracks(bool archived);
  void updateTrackMarker(GpxFile* track);
  void showTrack(GpxFile* track, bool show);
  void setTrackVisible(GpxFile* track, bool visible);
  void populateArchived();
  void populateTrackList();
  void populateTrack(GpxFile* track);
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
  void updateStats(GpxFile* track);
  void updateDistances();
  bool findPickedWaypoint(GpxFile* track);
  void toggleRouteEdit(bool show);
  Widget* createEditDialog(Button* editTrackBtn);
  void editWaypoint(GpxFile* track, const Waypoint& wpt, std::function<void()> callback);
  void setStatsText(const char* selector, std::string str);
  void setTrackEdit(bool show);
  bool saveTrack(GpxFile* track);
  enum TrackView_t { TRACK_NONE=-1, TRACK_STATS=0, TRACK_PLOT, TRACK_WAYPTS };
  void setTrackWidgets(TrackView_t view);
  void startRecording();
  void closeActiveTrack();
  // UI setup
  void createStatsContent();
  void createPlotContent();
  void createWayptContent();
  void createTrackListPanel();
  void createTrackPanel();

  Widget* trackPanel = NULL;
  Toolbar* trackToolbar = NULL;
  Widget* trackContainer = NULL;
  Menu* trackOverflow = NULL;
  std::vector<Widget*> statsWidgets;
  std::vector<Widget*> plotWidgets;
  std::vector<Widget*> wayptWidgets;

  DragDropList* tracksContent = NULL;
  Widget* archivedContent = NULL;
  Widget* tracksPanel = NULL;
  Widget* archivedPanel = NULL;
  Widget* statsContent = NULL;
  DragDropList* wayptContent = NULL;
  TrackPlot* trackPlot = NULL;
  Button* pauseRecordBtn = NULL;
  Button* stopRecordBtn = NULL;
  Button* saveCurrLocBtn = NULL;
  Button* saveRouteBtn = NULL;
  Button* routeModeBtn = NULL;
  Button* routePluginBtn = NULL;
  TextBox* previewDistText = NULL;
  Widget* sparkStats = NULL;
  TrackSparkline* trackSpark = NULL;
  Button* retryBtn = NULL;
  Button* routeEditBtn = NULL;
  Toolbar* routeEditTb = NULL;
  Button* plotVsTimeBtn = NULL;
  Widget* liveStatsRow = NULL;
  Widget* nonliveStatsRow = NULL;
  Widget* wayptTabLabel = NULL;
  Toolbar* editTrackTb = NULL;
  Widget* editTrackContent = NULL;
  TrackSliders* trackSliders = NULL;

  std::unique_ptr<FileStream> recordGPXStrm;
  Timer* recordTimer = NULL;
  int pluginFn = 0;
  std::vector<Waypoint> origLocs;
  std::string insertionWpt;
  std::string trackSummary;
  Timestamp lastTrackPtTime;
  Waypoint trackHoverLoc = LngLat{0, 0};
  double cropStart = 0;
  double cropEnd = 1;
  double recordLastSave = 0;
  bool recordTrack = false;
  //bool drawTrack = false;
  bool directRoutePreview = false;
  bool tracksDirty = true;
  bool archiveDirty = true;
  bool waypointsDirty = true;
  bool plotDirty = true;
  bool showAllWaypts = false;
  bool archiveLoaded = false;
  bool tapToAddWaypt = false;
  bool replaceWaypt = false;  // replacing waypt from search or bookmarks
  bool stealPickResult = false;  // adding waypt from search or bookmarks
  std::unique_ptr<SelectDialog> selectTrackDialog;
  std::unique_ptr<Dialog> editWayptDialog;
  std::unique_ptr<Dialog> newTrackDialog;
};
