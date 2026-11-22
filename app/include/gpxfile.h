#pragma once

#include "mapscomponent.h"
#include "ulib/platformutil.h"

class UniqueMarkerID
{
public:
  MarkerID handle = 0;
  UniqueMarkerID(MarkerID v) : handle(v) {}
  UniqueMarkerID(const UniqueMarkerID& other) : handle(0) {}
  UniqueMarkerID(UniqueMarkerID&& other) : handle(std::exchange(other.handle, 0)) {}
  ~UniqueMarkerID();
  UniqueMarkerID& operator=(UniqueMarkerID&& other) { std::swap(handle, other.handle); return *this; }
  UniqueMarkerID& operator=(const UniqueMarkerID& other) { return *this; }
  //UniqueMarkerID& operator=(MarkerID v) { std::swap(handle, other.handle); return *this; }
  operator MarkerID() const { return handle; }
};

struct Waypoint
{
  Location loc;
  double dist = 0;
  std::string name;
  std::string desc;
  std::string props;
  std::string sym;  // GPX <sym>; for route steps, maneuver keyword (see MapsTracks::addRouteStep) and name = street
  std::string uid;  // id for DragDropList
  UniqueMarkerID marker = 0;
  UniqueMarkerID labelMarker = 0;  // route step label (maneuver icon + street)
  //bool visible = true;  // <extensions><sl:route visible="true" routed="true"/>
  bool routed = true;

  Waypoint(const Location& _loc, const std::string& _name = "", const std::string& _desc = "",
      const std::string& _props = "") : loc(_loc), name(_name), desc(_desc), props(_props) {}
  Waypoint(const LngLat& _r, const std::string& _name = "", const std::string& _desc = "", const std::string& _props = "")
      : loc({0, _r.latitude, _r.longitude, 0, 0, 0, 0, 0, 0, 0}), name(_name), desc(_desc), props(_props) {}
  Waypoint(const Location& _loc, double _dist) : loc(_loc), dist(_dist) {}
  //~Waypoint();
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

// this is pretty ugly - we just shouldn't have a non-copyable object as a member of GpxFile at all
struct TrackMarker
{
  Properties markerProps;
  uint64_t featureId = -1;

  TrackMarker() {}
  TrackMarker(const TrackMarker& other) {}
  TrackMarker(TrackMarker&& other) :
      markerProps(std::move(other.markerProps)), featureId(std::exchange(other.featureId, -1)) {}
  TrackMarker& operator=(TrackMarker&& other)
      { std::swap(markerProps, other.markerProps); std::swap(featureId, other.featureId); return *this; }
  TrackMarker& operator=(const TrackMarker& other) { return *this; }

  ~TrackMarker();
  bool isValid() const { return featureId != uint64_t(-1); }
  void setProperties(Properties&& props, bool replace = false);
  void setTrack(GpxWay* way, size_t nways = 1);
};

struct GpxFile
{
  std::string title;
  std::string desc;
  std::string filename;
  std::string style;
  std::string routeMode;
  TrackMarker marker;

  std::vector<Waypoint> waypoints;
  std::vector<GpxWay> routes;
  std::vector<GpxWay> tracks;

  double timestamp = mSecSinceEpoch()/1000.0;
  int rowid = -1;
  int wayPtSerial = 0;
  bool visible = false;
  bool archived = false;
  bool loaded = false;
  bool modified = false;
  bool hasSpeed = false;

  GpxFile() {}
  GpxFile(const std::string& _title, const std::string& _desc, const std::string& _file,
      const std::string& _style = "", int _rowid = -1, bool _archived = false)
      : title(_title), desc(_desc), filename(_file), style(_style), rowid(_rowid), archived(_archived) {}

  GpxWay* activeWay() { return !routes.empty() ? &routes.back() : !tracks.empty()? &tracks.back() : NULL; }

  std::vector<Waypoint>::iterator findWaypoint(const std::string& uid);
  std::vector<Waypoint>::iterator addWaypoint(Waypoint wpt, const std::string& nextuid = {});
};

// Track statistics (distance, moving time, ascent/descent, ...) for a list of points, calculated
//  incrementally: update() processes only points added since the previous call, so a persistent TrackStats
//  can be kept for a track being recorded.  A fresh TrackStats gives a full calculation.
// - update() returns stats for all points; the last point always counts toward distance (even if within
//  position error of previous counted point), so it is processed provisionally on a copy and only committed
//  once another point is added
// - update() also sets Waypoint::dist and, if !hasSpeed, Location::spd (estimated speed) for each point
// - must be reset (by assigning a fresh TrackStats) if any processed points are modified or removed
struct TrackStats
{
  // results (meters, seconds, meters/sec)
  double trackDist = 0;  // for tracks, only moves larger than position error are counted
  double rawDist = 0;
  double movingTime = 0;  // tracks only (routes use total time)
  double movingTimeGps = 0;  // time w/ GPS speed > 0.1 m/s (only if hasSpeed)
  double maxSpeed = 0;
  double trackAscent = 0, trackDescent = 0;  // trackDescent <= 0
  double ascentTime = 0, descentTime = 0;

  // parameters
  bool isTrack = true;  // false for route
  bool hasSpeed = false;  // points have GPS speed (otherwise speed is estimated from position)
  double speedInvTau = 0.5;  // 1/time constant for estimated speed smoothing

  // state for incremental calculation
  size_t nextIdx = 1;  // next point to commit
  size_t prevDistLoc = 0, prevVertLoc = 0;
  double estSpeed = 0;

  TrackStats(bool _isTrack = true, bool _hasSpeed = false, double _speedInvTau = 0.5)
      : isTrack(_isTrack), hasSpeed(_hasSpeed), speedInvTau(_speedInvTau) {}
  TrackStats update(std::vector<Waypoint>& locs);

private:
  void addPoint(std::vector<Waypoint>& locs, size_t ii, bool islast);
};

namespace pugi { class xml_node; }
void saveWaypoint(pugi::xml_node trkpt, const Waypoint& wpt, bool savespd = false, bool savedist = false);
bool loadGPX(GpxFile* track, const char* gpxSrc = NULL);
bool saveGPX(GpxFile* track, const char* filename = NULL);
std::vector<Waypoint> decodePolylineStr(const std::string& encoded, double precision = 1E6);
