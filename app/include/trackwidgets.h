#pragma once

#include "tracks.h"
#include "ugui/widgets.h"

class TrackPlot : public CustomWidget
{
public:
  TrackPlot();
  void draw(SvgPainter* svgp) const override;
  void setTrack(const std::vector<Waypoint>& locs, const std::vector<Waypoint>& wpts);
  real plotPosToTrackPos(real s) const;
  real trackPosToPlotPos(real s) const;

  std::function<void(real)> onHovered;
  std::function<void()> onPanZoom;

  Path2D altDistPlot, altTimePlot, spdDistPlot, spdTimePlot;
  std::vector<Waypoint> waypoints;
  double minAlt = 0, maxAlt = 0;
  double minSpd = 0, maxSpd = 0;
  double minTime = 0, maxTime = 0;
  double maxDist = 1;
  bool plotVsDist = true;
  bool plotAlt = true;
  bool plotSpd = false;
  bool vertAxis = false;

  real zoomScale = 1;
  real zoomOffset = 0;
  real minOffset = 0;
  real maxZoom = 1;

  //static Color bgColor;

private:
  real prevCOM = 0;
  real prevPinchDist = 0;
  mutable real plotWidth = 100;

  void updateZoomOffset(real dx);
};

class TrackSparkline : public CustomWidget
{
public:
  //TrackSparkline() {}
  void draw(SvgPainter* svgp) const override;
  void setTrack(const std::vector<Waypoint>& locs);

  Path2D altDistPlot;
  double minAlt, maxAlt;
  double maxDist;
};

class TrackSliders : public Slider
{
public:
  enum {NO_UPDATE = 0, UPDATE = 1, FORCE_UPDATE = 2};
  TrackSliders(SvgNode* n);
  void setEditMode(bool editmode);
  void setCropHandles(real start, real end, int update);
  std::function<void()> onStartHandleChanged;
  std::function<void()> onEndHandleChanged;

  real startHandlePos = 0;
  real endHandlePos = 0;  // this reflects initial state of widget

private:
  Widget* startHandle;
  Widget* endHandle;
};

TrackSliders* createTrackSliders();
