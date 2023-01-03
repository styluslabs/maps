#pragma once

#include "mapscomponent.h"

class MapsBookmarks : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  void showGUI();
  void hideBookmarks();
  void addBookmark(const char* list, const char* osm_id, const char* props, const char* note, LngLat lnglat);

  std::vector<MarkerID> bkmkMarkers;

private:
  void showPlacesGUI();
  void showViewsGUI();
};

