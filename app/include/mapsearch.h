#pragma once

#include "mapscomponent.h"
#include "yaml-cpp/yaml.h"
#include "rapidjson/document.h"

#include "ugui/svggui.h"

using Tangram::TileTask;

struct SearchData {
  std::string layer;
  std::vector<std::string> fields;
};

struct SearchResult
{
  int64_t id;
  LngLat pos;
  float rank;
  MarkerID markerId;
  bool isPinMarker;
  rapidjson::Document tags;  // will eventually be a DuktapeValue? standard osm tag names for now
};

class TextEdit;
class Button;

class MapsSearch : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  void showGUI();
  void clearSearch();
  bool indexMBTiles();

  static void indexTileData(TileTask* task, int mapId, const std::vector<SearchData>& searchData);
  static std::vector<SearchData> parseSearchFields(const YAML::Node& node);

  SearchResult& addListResult(int64_t id, double lng, double lat, float rank);
  SearchResult& addMapResult(int64_t id, double lng, double lat, float rank);

  std::mutex resultsMutex;
  std::vector<MarkerID> pinMarkers;
  std::vector<MarkerID> dotMarkers;

  int providerIdx = 0;
  // search flags
  enum { MAP_SEARCH = 1, SORT_BY_DIST = 2 };

  enum SearchPhase { EDITING, RETURN, NEXTPAGE };
  void searchText(std::string query, SearchPhase phase);
  void onMapChange();
  void updateMapResults(LngLat lngLat00, LngLat lngLat11);
  void resultsUpdated();

  Widget* createPanel();

private:
  std::atomic_int tileCount;

  std::vector<SearchResult> listResults;
  std::vector<SearchResult> mapResults;

  //float markerRadius = 50;  // in pixels
  float prevZoom = 0;
  LngLat dotBounds00, dotBounds11;
  std::string searchStr;

  bool moreMapResultsAvail = false;
  bool mapResultsChanged = false;  // protected by resultsMutex

  void offlineListSearch(std::string queryStr, LngLat, LngLat);
  void offlineMapSearch(std::string queryStr, LngLat lnglat00, LngLat lngLat11);

  void onlineListSearch(std::string queryStr, LngLat lngLat00, LngLat lngLat11);
  void onlineMapSearch(std::string queryStr, LngLat lngLat00, LngLat lngLat11);
  void onlineSearch(std::string queryStr, LngLat lngLat00, LngLat lngLat11, bool isMapSearch);

  void onZoom();
  void clearSearchResults(std::vector<SearchResult>& results);
  MarkerID getPinMarker(const SearchResult& res, int idx);
  MarkerID getDotMarker(const SearchResult& res);
  void createMarkers();

  // GUI
  void populateAutocomplete(const std::vector<std::string>& history);
  void populateResults(const std::vector<SearchResult>& results);

  Widget* searchPanel = NULL;
  Widget* resultsContent = NULL;
  ///
  ///Widget* autoCompList;
  ///Widget* autoCompContainer;
  Button* cancelBtn;
  TextEdit* queryText;
  std::unique_ptr<SvgNode> searchResultProto;
  std::unique_ptr<SvgNode> autoCompProto;
  //std::unique_ptr<SvgNode> historyIconNode;
  //std::unique_ptr<SvgNode> resultIconNode;
};
