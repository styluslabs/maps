#pragma once

#include "mapscomponent.h"

class SvgNode;
class Widget;

class MapsBookmarks : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  //void showGUI();
  void hideBookmarks();
  void addBookmark(const char* list, const char* osm_id, const char* props, const char* note, LngLat lnglat, int rowid = -1);
  void onMapChange();

  Widget* createPanel();
  Widget* getPlaceInfoSection(const std::string& osm_id, LngLat pos);

  std::vector<MarkerID> bkmkMarkers;

private:
  //void showPlacesGUI();
  //void showViewsGUI();

  void populateBkmks(const std::string& listname);
  void populateLists();

  Widget* bkmkPanel = NULL;
  Widget* bkmkContent = NULL;
  Widget* listsPanel = NULL;
  Widget* listsContent = NULL;
  bool mapAreaBkmks = false;

  std::unique_ptr<SvgNode> bkmkListProto;
  std::unique_ptr<SvgNode> placeListProto;
  std::unique_ptr<SvgNode> placeInfoSectionProto;
};

