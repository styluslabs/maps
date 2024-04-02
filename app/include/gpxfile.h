#pragma once

#include "mapscomponent.h"

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
  std::string uid;  // id for DragDropList
  UniqueMarkerID marker = 0;
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

class TrackMarker
{
public:
  Map* map;
  std::vector<MarkerID> markers;
  std::string stylePath;
  Properties markerProps;
  double maxExtent = 1000;  // meters

  TrackMarker(Map* _map, const char* style) : map(_map), stylePath(style) {}
  ~TrackMarker();
  void setVisible(bool vis);
  void setStylePath(const char* style);
  void setProperties(Properties&& props);
  void setTrack(GpxWay* way, size_t nways = 1);
  bool onPicked(MarkerID picked);
};

struct GpxFile {
  std::string title;
  std::string desc;
  std::string filename;
  std::string style;
  std::string routeMode;
  std::unique_ptr<TrackMarker> marker;

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
  GpxFile(const std::string& _title, const std::string& _desc, const std::string& _file,
      const std::string& _style = "", int _rowid = -1, bool _archived = false)
      : title(_title), desc(_desc), filename(_file), style(_style), rowid(_rowid), archived(_archived) {}

  GpxWay* activeWay() { return !routes.empty() ? &routes.back() : !tracks.empty()? &tracks.back() : NULL; }

  std::vector<Waypoint>::iterator findWaypoint(const std::string& uid);
  std::vector<Waypoint>::iterator addWaypoint(Waypoint wpt, const std::string& nextuid = {});
};

bool loadGPX(GpxFile* track, const char* gpxSrc = NULL);
bool saveGPX(GpxFile* track, const char* filename = NULL);
