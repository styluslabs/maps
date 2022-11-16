#pragma once

#include "mapsapp.h"

class MapsBookmarks : public MapsComponent
{
public:
  void showGUI();
  void hideBookmarks();

  std::vector<MarkerID> bkmkMarkers;
};

