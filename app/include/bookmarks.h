#pragma once

#include "mapscomponent.h"
#include "util.h"

class SvgNode;
class SvgDocument;
class Widget;

class MapsBookmarks : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  //void showGUI();
  void hideBookmarks(int excludelist = -1);
  void restoreBookmarks();
  void addBookmark(int list_id, const char* osm_id, const char* name, const char* props, const char* note, LngLat pos, int timestamp = -1); //, int rowid = -1);
  int getListId(const char* listname, bool create = false);
  void onMapChange();

  Widget* createPanel();
  Widget* getPlaceInfoSection(const std::string& osm_id, LngLat pos);

  //std::vector<MarkerID> bkmkMarkers;
  std::unordered_map< int, std::unique_ptr<MarkerGroup> > bkmkMarkers;

private:
  //void showPlacesGUI();
  //void showViewsGUI();

  void populateBkmks(int list_id, bool createUI);
  void populateLists(bool archived);
  Widget* getPlaceInfoSubSection(int rowid, int listid, std::string namestr, std::string notestr);
  void chooseBookmarkList(std::function<void(int, std::string)> callback);

  Widget* bkmkPanel = NULL;
  Widget* bkmkContent = NULL;
  Widget* listsPanel = NULL;
  Widget* listsContent = NULL;
  Widget* archivedPanel = NULL;
  Widget* archivedContent = NULL;
  bool mapAreaBkmks = false;
  //std::vector<MarkerGroup*> hiddenGroups;
  bool bkmkPanelDirty = false;
  bool listsDirty = false;
  bool archiveDirty = false;
  int activeListId = -1;

  std::unique_ptr<SvgNode> bkmkListProto;
  std::unique_ptr<SvgNode> listSelectProto;
  std::unique_ptr<SvgNode> placeListProto;
  std::unique_ptr<SvgNode> placeInfoSectionProto;
  std::unique_ptr<SvgDocument> chooseListProto;
};

