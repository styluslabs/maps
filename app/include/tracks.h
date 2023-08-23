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

struct GpxWay
{
  std::string title;
  std::string desc;
  std::vector<Waypoint> pts;

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
  bool visible = true;
  bool archived = false;
  //bool loaded = false;

  GpxFile(const std::string& _title, const std::string& _desc, const std::string& _file)
      : title(_title), desc(_desc), filename(_file) {}

  GpxWay* activeWay() { return !routes.empty() ? &routes.front() : !tracks.empty()? &tracks.front() : NULL; }
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

  std::vector<GpxFile> tracks;
  GpxFile recordedTrack;
  //GpxFile drawnTrack;

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

  static GpxFile loadGPX(const char* gpxfile);
  static bool saveGPX(GpxFile* track);

private:
  void loadTracks(bool archived);
  void updateTrackMarker(GpxFile* track);
  void showTrack(GpxFile* track, bool show);
  void setTrackVisible(GpxFile* track, bool visible);
  void populateTracks(bool archived);
  void populateStats(GpxFile* track);
  Widget* createTrackEntry(GpxFile* track);
  Waypoint interpTrack(const std::vector<Waypoint>& locs, double s, size_t* idxout = NULL);

  GpxFile* activeTrack = NULL;
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
