#pragma once

#include "mapscomponent.h"
#include "yaml-cpp/yaml.h"

class MapsSources : public MapsComponent
{
public:
  MapsSources(MapsApp* _app, const std::string& sourcesFile);
  void showGUI();

  void addSource(const std::string& key, YAML::Node srcnode);

private:
  std::mutex sourcesMutex;
  std::string baseUrl;
  YAML::Node mapSources;
  std::atomic<bool> sourcesLoaded{false};
};
