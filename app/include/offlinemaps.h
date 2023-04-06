#pragma once

#include "mapscomponent.h"

class Widget;
class SvgNode;

class MapsOffline : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  ~MapsOffline();
  void showGUI();

  void saveOfflineMap(int maxZoom);

  Widget* createPanel();

  Widget* offlinePanel;

private:
  std::unique_ptr<SvgNode> offlineListProto;
  MarkerID rectMarker = 0;
};
