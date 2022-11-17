#pragma once

#include "mapscomponent.h"

class MapsSearch : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  void showGUI();
  void clearSearch();

  std::vector<MarkerID> searchMarkers;
  std::vector<MarkerID> dotMarkers;
};

