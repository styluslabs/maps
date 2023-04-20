#pragma once

#include "mapscomponent.h"
#include "util.h"

class SvgNode;
class Widget;

class MapsBookmarks : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  //void showGUI();
  void hideBookmarks(const std::string& excludelist = "");
  void restoreBookmarks();
  void addBookmark(const char* list, const char* osm_id, const char* props, const char* note, LngLat lnglat, int rowid = -1);
  void onMapChange();

  Widget* createPanel();
  Widget* getPlaceInfoSection(const std::string& osm_id, LngLat pos);

  //std::vector<MarkerID> bkmkMarkers;
  std::unordered_map< std::string, std::unique_ptr<MarkerGroup> > bkmkMarkers;

private:
  //void showPlacesGUI();
  //void showViewsGUI();

  void populateBkmks(const std::string& listname, bool createUI);
  void populateLists(bool archived);

  Widget* bkmkPanel = NULL;
  Widget* bkmkContent = NULL;
  Widget* listsPanel = NULL;
  Widget* listsContent = NULL;
  Widget* archivedPanel = NULL;
  Widget* archivedContent = NULL;
  bool mapAreaBkmks = false;
  std::vector<MarkerGroup*> hiddenGroups;
  bool bkmkPanelDirty = false;
  bool listsDirty = false;
  bool archiveDirty = false;

  std::unique_ptr<SvgNode> bkmkListProto;
  std::unique_ptr<SvgNode> placeListProto;
  std::unique_ptr<SvgNode> placeInfoSectionProto;
};

