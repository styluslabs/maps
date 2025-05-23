#pragma once

#include "mapscomponent.h"
#include "util.h"

struct SearchResult;

class MapsBookmarks : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  void hideBookmarks(int excludelist = -1);
  void restoreBookmarks();
  int addBookmark(int list_id, const std::string& osm_id, const std::string& name,
      const std::string& props, const std::string& note, LngLat pos, int timestamp = -1);
  int getListId(const char* listname, bool create = false);
  void onMapEvent(MapEvent_t event);
  Button* createPanel();
  void setPlaceInfoSection(const std::string& osm_id, LngLat pos);
  void addPlaceActions(Toolbar* tb);
  void createFromSearch(const std::string& title, const std::vector<SearchResult>& results);

  Widget* listsPanel = NULL;
  std::unordered_map< int, std::unique_ptr<MarkerGroup> > bkmkMarkers;

private:
  void populateBkmks(int list_id, bool createUI);
  void populateLists(bool archived);
  Widget* getPlaceInfoSubSection(int rowid, int listid, std::string namestr, std::string notestr);
  void chooseBookmarkList(std::function<void(int, std::string)> callback);
  void deleteBookmark(int listid, int rowid);
  Color nextListColor();
  void importGpx(const char* filename, const char* gpxsrc = NULL);
  void importImages(int64_t list_id, const char* path);
  void startImageImport(std::string title, std::string path);
  void exportGpx(const char* filename, int listid);
  void editBookmark(int rowid, int listid, std::function<void()> callback);
  void deleteList(int rowid, const std::string& title, bool clearOnly);
  void resultStepper(int list_id, int rowid, int dir);

  Widget* bkmkPanel = NULL;
  Widget* bkmkContent = NULL;
  DragDropList* listsContent = NULL;
  Widget* archivedPanel = NULL;
  Widget* archivedContent = NULL;
  Dialog* importProgressDialog = NULL;
  bool mapAreaBkmks = false;
  bool bkmkPanelDirty = false;
  bool listsDirty = true;
  bool archiveDirty = false;
  int activeListId = -1;
  std::string activeListTitle;
  std::string activeListColor;

  std::unique_ptr<SvgNode> placeInfoSectionProto;
  //std::unique_ptr<SvgDocument> chooseListProto;
  std::unique_ptr<Dialog> chooseListDialog;
  std::unique_ptr<Dialog> newListDialog;
  std::unique_ptr<Dialog> editListDialog;
  std::unique_ptr<Dialog> editPlaceDialog;
};

