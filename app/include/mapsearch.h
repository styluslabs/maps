#pragma once

#include "mapscomponent.h"
#include "yaml-cpp/yaml.h"

struct SearchData {
  std::string layer;
  std::vector<std::string> fields;
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
};

