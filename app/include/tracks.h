#pragma once

#include <list>
#include "mapscomponent.h"
#include "gpxfile.h"
#include "ulib/fileutil.h"
#include "isect2d.h"
#include "glm/vec2.hpp"

class TrackPlot;
class TrackSparkline;
class TrackSliders;
class PlatformFile;
struct Timer;

class MapsTracks : public MapsComponent {
public:
  using MapsComponent::MapsComponent;
  Button* createPanel();
  void addPlaceActions(Toolbar* tb);
  void updateLocation(const Location& loc);
  void onMapEvent(MapEvent_t event);
  void addRoute(std::vector<Waypoint>&& route);
  // route step at route pt rteptidx; turn = maneuver keyword: start, continue, slight_left, left, sharp_left,
  //  keep_left, slight_right, right, sharp_right, keep_right, uturn, roundabout, arrive; street, turn optional; if
  //  instr is empty, it's generated from turn and street
  void addRouteStep(const char* instr, int rteptidx, const char* turn = "", const char* street = "");
  bool onPickResult();
  bool tapEvent(LngLat location);
  void fingerEvent(int action, LngLat pos);
  bool dragAutoPan(double dt);
  void routePluginError(const char* err);
  bool onFeaturePicked(const Tangram::FeaturePickResult* result);
  void loadTrackGPX(std::unique_ptr<PlatformFile> file);

  Widget* tracksPanel = NULL;

  MarkerID trackHoverMarker = 0;
  MarkerID trackStartMarker = 0;
  MarkerID trackEndMarker = 0;

  std::list<GpxFile> tracks;
  GpxFile recordedTrack;
  GpxFile navRoute;
  GpxFile* activeTrack = NULL;
  bool recordTrack = false;

  double currSpeed = 0;
  double speedInvTau = 0.5;
  double minTrackDist = 2;  // meters
  double minTrackTime = 5;  // seconds
  float gpsSamplePeriod = 1.0f;  // seconds

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
  void updateNotification();
  Waypoint* nearestRoutePt(const Waypoint& wpt);
  void updateDistances();
  bool findPickedWaypoint(GpxFile* track);
  void toggleRouteEdit(bool show);
  void newRoute(std::string mode);
  void createEditDialog();
  void editWaypoint(GpxFile* track, const Waypoint& wpt, std::function<void()> callback);
  void setStatsText(const char* selector, std::string str);
  void setTrackEdit(bool show);
  bool saveTrack(GpxFile* track);
  enum TrackView_t { TRACK_NONE=-1, TRACK_STATS=0, TRACK_PLOT, TRACK_WAYPTS };
  void setTrackWidgets(TrackView_t view);
  void startRecording();
  void closeActiveTrack();
  void hideDirectRoutePreview();
  // undo/redo for waypoint edits: history of active track's waypoint lists, saved after each edit
  void resetHistory();
  void saveHistory(bool replaceLast = false);
  void restoreHistory(size_t pos);
  void updateUndoBtns();
  // UI setup
  void createStatsContent();
  void createPlotContent();
  void createWayptContent();
  void createTrackListPanel();
  void createTrackPanel();
  void startWayptDrag(Waypoint* wp);
  void updateWayptDrag();
  glm::vec2 dragEdgeVel(glm::vec2 p);

  Toolbar* trackToolbar = NULL;
  Widget* trackContainer = NULL;
  Menu* trackOverflow = NULL;
  std::vector<Widget*> statsWidgets;
  std::vector<Widget*> plotWidgets;
  std::vector<Widget*> wayptWidgets;

  DragDropList* tracksContent = NULL;
  Widget* archivedContent = NULL;
  Widget* trackPanel = NULL;
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
  Widget* previewDistText = NULL;
  Widget* sparkStats = NULL;
  TrackSparkline* trackSpark = NULL;
  Button* retryBtn = NULL;
  Button* routeEditBtn = NULL;
  Widget* routeEditTb = NULL;  // AutoAdjContainer w/ route edit toolbar
  Button* plotVsTimeBtn = NULL;
  Widget* liveStatsRow = NULL;
  Widget* nonliveStatsRow = NULL;
  Widget* wayptTabLabel = NULL;
  Toolbar* editTrackTb = NULL;
  //Widget* editTrackContent = NULL;
  TrackSliders* trackSliders = NULL;
  Button* tracksBtn = NULL;
  Button* recordTrackBtn = NULL;
  Button* plotEditBtn = NULL;
  Button* insertWayptBtn = NULL;
  Button* undoBtn = NULL;
  Button* redoBtn = NULL;

  std::unique_ptr<FileStream> recordGPXStrm;
  Timer* recordTimer = NULL;
  int pluginFn = 0;
  std::unique_ptr<GpxFile> origTrack;
  std::string insertionWpt;
  std::string trackSummary;
  TrackStats recordStats;  // incremental stats for recordedTrack while recording
  Timestamp notifyTime = 0;  // last update of recording notification
  size_t sparkRecordPts = SIZE_MAX;  // recorded track size at last sparkline update (SIZE_MAX if other track)
  Timestamp lastTrackPtTime;
  Waypoint trackHoverLoc = LngLat{0, 0};
  double sliderPos = 0;
  double cropStart = 0;
  double cropEnd = 1;
  double recordLastSave = 0;
  size_t nElevPending = 0;
  //bool drawTrack = false;
  bool directRoutePreview = false;
  bool tracksDirty = true;
  bool archiveDirty = true;
  bool waypointsDirty = true;
  bool plotDirty = true;
  bool showAllWaypts = false;
  bool archiveLoaded = false;
  bool tapToAddWaypt = false;
  bool autoInsertWaypt = false;
  bool replaceWaypt = false;  // replacing waypt from search or bookmarks
  bool stealPickResult = false;  // adding waypt from search or bookmarks
  std::unique_ptr<SelectDialog> selectTrackDialog;
  std::unique_ptr<Dialog> editWayptDialog;
  std::unique_ptr<Dialog> newTrackDialog;
  std::unique_ptr<Dialog> editTrackDialog;

  isect2d::ISect2D<glm::vec2> routeCollider;
  glm::dvec2 routeOrigin;
  Waypoint* dragWaypt = NULL;
  bool dragInserted = false;  // dragWaypt was just inserted (long press on route), so drag is same undo step
  glm::vec2 dragOffset;  // marker screen pos - finger pos at drag start
  LngLat dragPos;  // current position of dragged marker
  bool dragPanArmed = false;  // finger has been outside edge zone, so auto-pan is allowed
  std::vector<std::vector<Waypoint>> wayptHistory;  // [histPos] is current state; empty if no active track
  size_t histPos = 0;
};
