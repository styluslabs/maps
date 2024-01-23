#pragma once

#include "mapscomponent.h"

struct Waypoint
{
  Location loc;
  double dist = 0;
  std::string name;
  std::string desc;
  std::string props;
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

bool loadGPX(GpxFile* track, const char* gpxSrc = NULL);
bool saveGPX(GpxFile* track);
