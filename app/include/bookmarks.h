#pragma once

#include "mapscomponent.h"

class MapsBookmarks : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  void showGUI();
  void hideBookmarks();

  std::vector<MarkerID> bkmkMarkers;

private:
  void showPlacesGUI();
  void showViewsGUI();
};

