#pragma once

#include "mapscomponent.h"

class SvgNode;
class Widget;

class MapsBookmarks : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  void showGUI();
  void hideBookmarks();
  void addBookmark(const char* list, const char* osm_id, const char* props, const char* note, LngLat lnglat, int rowid = -1);

  std::vector<MarkerID> bkmkMarkers;

private:
  void showPlacesGUI();
  void showViewsGUI();

  Widget* bkmkPanel = NULL;
  Widget* bkmkContent = NULL;
  Widget* listsPanel = NULL;
  Widget* listsContent = NULL;

  std::unique_ptr<SvgNode> bkmkListProto;
  std::unique_ptr<SvgNode> placeListProto;
  std::unique_ptr<SvgNode> placeInfoSectionProto;
};

