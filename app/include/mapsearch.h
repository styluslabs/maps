#pragma once

#include "mapscomponent.h"
//#include "util.h"
#include "yaml-cpp/yaml.h"
#include "scene/filters.h"

#include "ugui/svggui.h"

using Tangram::TileTask;
class MarkerGroup;
class SQLiteDB;

struct SearchData {
  std::string layer;
  std::vector<std::string> fields;
  Tangram::Filter filter;
};

struct SearchResult
{
  int64_t id;
  LngLat pos;
  float rank;
  std::string tags;
};

class MapsSearch : public MapsComponent
{
public:
  MapsSearch(MapsApp* _app);
  ~MapsSearch();  // to allow unique_ptr to incomplete class
  void clearSearch();
  bool indexMBTiles();
  void addListResult(int64_t id, double lng, double lat, float rank, const char* json);
  void addMapResult(int64_t id, double lng, double lat, float rank, const char* json);
  void searchPluginError(const char* err);

  enum SearchPhase { EDITING, RETURN, NEXTPAGE };
  void searchText(std::string query, SearchPhase phase);
  void onMapEvent(MapEvent_t event);
  void resultsUpdated(int flags);
  void doSearch(std::string query);

  Button* createPanel();
  Widget* searchPanel = NULL;
  std::unique_ptr<MarkerGroup> markers;
  int providerIdx = 0;
  bool moreMapResultsAvail = false;
  bool moreListResultsAvail = false;
  // search flags
  enum { MAP_SEARCH = 0x1, LIST_SEARCH = 0x2, SORT_BY_DIST = 0x4, FLY_TO = 0x8, UPDATE_RESULTS = 0x4000, MORE_RESULTS = 0x8000 };

  static void indexTileData(TileTask* task, int mapId, const std::vector<SearchData>& searchData);
  static void importPOIs(std::string srcuri, int offlineId);
  static void onDelOfflineMap(int mapId);
  static std::vector<SearchData> parseSearchFields(const YAML::Node& node);

  static SQLiteDB searchDB;

private:
  std::atomic_int tileCount;

  std::vector<SearchResult> listResults;
  std::vector<SearchResult> mapResults;

  //float markerRadius = 50;  // in pixels
  float prevZoom = 0;
  LngLat dotBounds00, dotBounds11;
  std::string searchStr;
  bool searchOnMapMove = true;
  bool unifiedSearch = false;
  bool flyingToResults = false;
  bool newMapSearch = true;
  bool isCurrLocDistOrigin = true;
  int selectedResultIdx = -1;

  bool initSearch();
  void offlineListSearch(std::string queryStr, LngLat, LngLat);
  void offlineMapSearch(std::string queryStr, LngLat lnglat00, LngLat lngLat11);
  void updateMapResultBounds(LngLat lngLat00, LngLat lngLat11);
  void updateMapResults(LngLat lngLat00, LngLat lngLat11, int flags);
  void clearSearchResults();

  // GUI
  void populateAutocomplete(const std::vector<std::string>& history);
  void populateResults();  //const std::vector<SearchResult>& results);

  Widget* resultsContent = NULL;
  Button* cancelBtn = NULL;
  Button* retryBtn = NULL;
  TextEdit* queryText = NULL;
  TextBox* resultCountText = NULL;
  Button* saveToBkmksBtn = NULL;
  size_t listResultOffset = 0;
};
