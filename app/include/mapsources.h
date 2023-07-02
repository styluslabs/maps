#pragma once

#include "mapscomponent.h"
#include "yaml-cpp/yaml.h"

class Widget;
class SelectBox;
class Button;

class MapsSources : public MapsComponent
{
public:
  MapsSources(MapsApp* _app);
  //void showGUI();

  void addSource(const std::string& key, YAML::Node srcnode);
  void rebuildSource(const std::string& srcname = "");
  void deleteOfflineMap(int mapid);
  int64_t shrinkCache(int64_t maxbytes);
  void onMapChange();
  Widget* createPanel();

  std::string currSource;

private:
  void createSource(std::string savekey, const std::string& newSrcTitle);
  void populateSources();
  void populateSceneVars();

  //std::mutex sourcesMutex;
  Widget* sourcesPanel;
  std::string baseUrl;
  YAML::Node mapSources;
  std::atomic<bool> sourcesLoaded{false};

  int nSources = 1;

  std::vector<Widget*> layerRows;
  std::vector<SelectBox*> layerCombos;
  SelectBox* sourceCombo = NULL;
  Button* discardBtn = NULL;
  Button* saveBtn = NULL;
  Widget* varsContent = NULL;
  bool sceneVarsLoaded = false;

  std::vector<std::string> layerKeys;
  std::vector<std::string> sourceKeys;
};
