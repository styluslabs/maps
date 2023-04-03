#pragma once

#include "mapscomponent.h"

class Widget;

class MapsOffline : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  ~MapsOffline();
  void showGUI();

  void saveOfflineMap(int maxZoom);

  Widget* createPanel();

  Widget* offlinePanel;
};
