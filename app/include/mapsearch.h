#pragma once

#include "mapscomponent.h"
#include "yaml-cpp/yaml.h"
#include "rapidjson/document.h"

struct SearchData {
  std::string layer;
  std::vector<std::string> fields;
};

struct SearchResult
{
  LngLat pos;
  float rank;
  MarkerID markerId;
  bool isPinMarker;
  rapidjson::Document tags;  // will eventually be a DuktapeValue? standard osm tag names for now
};

class MapsSearch : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  void showGUI();
  void clearSearch();
  bool indexMBTiles();

  static void indexTileData(TileTask* task, int mapId, const std::vector<SearchData>& searchData);
  static std::vector<SearchData> parseSearchFields(const YAML::Node& node);

  std::vector<MarkerID> searchMarkers;
  std::vector<MarkerID> dotMarkers;

private:
  std::atomic<int> tileCount;

  std::vector<SearchResult> results;

  size_t pinMarkerIdx = 0;
  size_t dotMarkerIdx = 0;

  SearchResult& addSearchResult(double lng, double lat, float rank);
};

