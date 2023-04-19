#pragma once

#include "mapscomponent.h"
#include "yaml-cpp/yaml.h"

class Widget;
class ComboBox;
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

  Widget* sourcesPanel;

  Widget* createPanel();

private:
  void createSource(std::string savekey, const std::string& newSrcTitle);
  void populateSources();
  void populateSceneVars();

  std::mutex sourcesMutex;
  std::string baseUrl;
  YAML::Node mapSources;
  std::atomic<bool> sourcesLoaded{false};

  int nSources = 1;

  std::vector<Widget*> layerRows;
  std::vector<ComboBox*> layerCombos;
  ComboBox* sourceCombo = NULL;
  Button* discardBtn = NULL;
  Button* saveBtn = NULL;
  Widget* varsContent = NULL;

  std::vector<std::string> layerKeys;
  std::vector<std::string> sourceKeys;

};
