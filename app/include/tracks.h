#pragma once

#include "mapscomponent.h"
#include "ulib/painter.h"  // for Color

class Widget;
class SvgNode;
class TrackPlot;
class SelectDialog;

class MapsTracks : public MapsComponent {
public:
  using MapsComponent::MapsComponent;
  Widget* createPanel();
  void tapEvent(LngLat location);
  void updateLocation(const Location& loc);

  MarkerID trackHoverMarker = 0;
  MarkerID trackStartMarker = 0;
  MarkerID trackEndMarker = 0;

  //struct TrackLoc : public Location { double dist; };
  using TrackLoc = Location;

  struct Track {
    std::string title;
    std::string detail;
    std::string gpxFile;
    std::string style;
    MarkerID marker;
    std::vector<TrackLoc> locs;
    int rowid;
    bool visible;
    bool archived;
  };

  std::vector<Track> tracks;
  Track recordedTrack;
  Track drawnTrack;

  Widget* tracksContent = NULL;
  Widget* tracksPanel = NULL;
  Widget* archivedContent = NULL;
  Widget* archivedPanel = NULL;
  Widget* statsContent = NULL;
  Widget* statsPanel = NULL;
  TrackPlot* trackPlot = NULL;

  double speedInvTau = 0.5;
  double minTrackDist = 2;  // meters
  double minTrackTime = 5;  // seconds

private:
  Track loadGPX(const char* gpxfile);
  bool saveGPX(Track* track);
  void showTrack(Track* track);  //, const char* styling);
  void populateTracks(bool archived);
  void populateStats(Track* track);
  Widget* createTrackEntry(Track* track);
  TrackLoc interpTrack(const std::vector<TrackLoc>& locs, double s, size_t* idxout = NULL);

  Track* activeTrack = NULL;
  std::vector<TrackLoc> origLocs;
  double recordLastSave = 0;
  bool recordTrack = false;
  bool drawTrack = false;
  bool tracksDirty = false;
  std::vector<Color> markerColors;
  std::unique_ptr<SvgNode> trackListProto;
  std::unique_ptr<SelectDialog> selectTrackDialog;
};
