#pragma once

#include "mapscomponent.h"

class Widget;
class SvgNode;

class MapsOffline : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  ~MapsOffline();
  //void showGUI();
  int numOfflinePending() const;
  void saveOfflineMap(int maxZoom);
  void updateProgress();

  Widget* createPanel();

  Widget* offlinePanel = NULL;

private:
  MarkerID rectMarker = 0;
  Widget* offlineContent = NULL;

  void populateOffline();
};
