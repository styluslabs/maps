#pragma once

#include "mapscomponent.h"
#include "util.h"

class SvgNode;
class SvgDocument;
class Widget;
class Button;
class DragDropList;
class Toolbar;

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

  std::unordered_map< int, std::unique_ptr<MarkerGroup> > bkmkMarkers;

private:
  void populateBkmks(int list_id, bool createUI);
  void populateLists(bool archived);
  Widget* getPlaceInfoSubSection(int rowid, int listid, std::string namestr, std::string notestr);
  void chooseBookmarkList(std::function<void(int, std::string)> callback);

  Widget* bkmkPanel = NULL;
  Widget* bkmkContent = NULL;
  Widget* listsPanel = NULL;
  DragDropList* listsContent = NULL;
  Widget* archivedPanel = NULL;
  Widget* archivedContent = NULL;
  bool mapAreaBkmks = false;
  bool bkmkPanelDirty = false;
  bool listsDirty = true;
  bool archiveDirty = false;
  int activeListId = -1;

  //std::unique_ptr<SvgNode> bkmkListProto;
  //std::unique_ptr<SvgNode> listSelectProto;
  //std::unique_ptr<SvgNode> placeListProto;
  std::unique_ptr<SvgNode> placeInfoSectionProto;
  std::unique_ptr<SvgDocument> chooseListProto;
};

