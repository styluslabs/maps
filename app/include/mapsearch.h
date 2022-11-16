#pragma once

#include "mapsapp.h"

class MapsSearch : public MapsComponent
{
public:
  void showGUI();
  void clearSearch();

  std::vector<MarkerID> searchMarkers;
  std::vector<MarkerID> dotMarkers;
};

