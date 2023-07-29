#pragma once

#include "mapscomponent.h"
#include "yaml-cpp/yaml.h"

class Widget;
class SelectBox;
class Button;
class TextEdit;
class SvgNode;

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
  std::string createSource(std::string savekey, const std::string& yamlStr = "");
  void populateSources();
  void populateSceneVars();
  void populateSourceEdit(std::string key);
  void sourceModified();
  void saveSources();

  Widget* sourcesPanel = NULL;
  Widget* sourceEditPanel = NULL;
  Widget* sourcesContent = NULL;
  std::string baseUrl;
  YAML::Node mapSources;
  std::atomic<bool> sourcesLoaded{false};

  int nSources = 1;

  std::vector<Widget*> layerRows;
  std::vector<SelectBox*> layerCombos;
  //SelectBox* sourceCombo = NULL;
  //Button* discardBtn = NULL;
  TextEdit* titleEdit = NULL;
  Button* saveBtn = NULL;
  Widget* varsContent = NULL;
  bool sceneVarsLoaded = false;
  std::vector<std::string> layerKeys;
  std::vector<std::string> sourceKeys;

  std::unique_ptr<SvgNode> sourceListProto;
};
