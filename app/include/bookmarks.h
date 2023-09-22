#pragma once

#include "mapscomponent.h"
#include "util.h"

class MapsBookmarks : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  void hideBookmarks(int excludelist = -1);
  void restoreBookmarks();
  int addBookmark(int list_id, const char* osm_id, const char* name, const char* props, const char* note, LngLat pos, int timestamp = -1); //, int rowid = -1);
  int getListId(const char* listname, bool create = false);
  void onMapEvent(MapEvent_t event);
  Button* createPanel();
  Widget* getPlaceInfoSection(const std::string& osm_id, LngLat pos);
  void addPlaceActions(Toolbar* tb);

  Widget* listsPanel = NULL;
  std::unordered_map< int, std::unique_ptr<MarkerGroup> > bkmkMarkers;

private:
  void populateBkmks(int list_id, bool createUI);
  void populateLists(bool archived);
  Widget* getPlaceInfoSubSection(int rowid, int listid, std::string namestr, std::string notestr);
  void chooseBookmarkList(std::function<void(int, std::string)> callback);
  void deleteBookmark(int listid, int rowid);

  Widget* bkmkPanel = NULL;
  Widget* bkmkContent = NULL;
  DragDropList* listsContent = NULL;
  Widget* archivedPanel = NULL;
  Widget* archivedContent = NULL;
  bool mapAreaBkmks = false;
  bool bkmkPanelDirty = false;
  bool listsDirty = true;
  bool archiveDirty = false;
  int activeListId = -1;

  std::unique_ptr<SvgNode> placeInfoSectionProto;
  std::unique_ptr<SvgDocument> chooseListProto;
};

