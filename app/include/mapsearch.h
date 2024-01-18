#pragma once

#include "mapscomponent.h"
#include "util.h"
#include "yaml-cpp/yaml.h"
#include "rapidjson/document.h"
#include "scene/filters.h"

#include "ugui/svggui.h"

using Tangram::TileTask;

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
  rapidjson::Document tags;  // will eventually be a DuktapeValue? standard osm tag names for now
};

class MapsSearch : public MapsComponent
{
public:
  MapsSearch(MapsApp* _app);
  void clearSearch();
  bool indexMBTiles();

  static void indexTileData(TileTask* task, int mapId, const std::vector<SearchData>& searchData);
  static void onDelOfflineMap(int mapId);
  static std::vector<SearchData> parseSearchFields(const YAML::Node& node);

  void addListResult(int64_t id, double lng, double lat, float rank, const char* json);
  void addMapResult(int64_t id, double lng, double lat, float rank, const char* json);
  void searchPluginError(const char* err);

  std::unique_ptr<MarkerGroup> markers;

  int providerIdx = 0;
  bool moreMapResultsAvail = false;
  bool moreListResultsAvail = false;
  // search flags
  enum { MAP_SEARCH = 0x1, LIST_SEARCH = 0x2, SORT_BY_DIST = 0x4, FLY_TO = 0x8, UPDATE_RESULTS = 0x4000, MORE_RESULTS = 0x8000 };

  enum SearchPhase { EDITING, RETURN, NEXTPAGE };
  void searchText(std::string query, SearchPhase phase);
  void onMapEvent(MapEvent_t event);
  void resultsUpdated(int flags);

  Button* createPanel();
  Widget* searchPanel = NULL;

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
  int selectedResultIdx = -1;

  void offlineListSearch(std::string queryStr, LngLat, LngLat);
  void offlineMapSearch(std::string queryStr, LngLat lnglat00, LngLat lngLat11);
  void updateMapResultBounds(LngLat lngLat00, LngLat lngLat11);
  void updateMapResults(LngLat lngLat00, LngLat lngLat11, int flags);
  void clearSearchResults();

  // GUI
  void populateAutocomplete(const std::vector<std::string>& history);
  void populateResults(const std::vector<SearchResult>& results);

  Widget* resultsContent = NULL;
  Button* cancelBtn = NULL;
  Button* retryBtn = NULL;
  TextEdit* queryText = NULL;
};
