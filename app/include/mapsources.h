#pragma once

#include "mapscomponent.h"
#include "yaml-cpp/yaml.h"

class Widget;
class SelectBox;
class Button;
class TextEdit;
class SvgNode;
class DragDropList;

class MapsSources : public MapsComponent
{
  friend class MapsOffline;
public:
  MapsSources(MapsApp* _app);
  //void showGUI();

  void addSource(const std::string& key, YAML::Node srcnode);
  void rebuildSource(const std::string& srcname = "");
  int64_t shrinkCache(int64_t maxbytes);
  void onMapEvent(MapEvent_t event);
  Button* createPanel();

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
  DragDropList* sourcesContent = NULL;
  std::string baseUrl;
  std::string srcFile;
  YAML::Node mapSources;
  std::atomic<bool> sourcesLoaded{false};
  //int nSources = 1;

  std::vector<Widget*> layerRows;
  std::vector<SelectBox*> layerCombos;
  //SelectBox* sourceCombo = NULL;
  //Button* discardBtn = NULL;
  TextEdit* titleEdit = NULL;
  Button* saveBtn = NULL;
  Widget* varsContent = NULL;
  bool sceneVarsLoaded = false;
  bool sourcesDirty = true;
  std::vector<std::string> layerKeys;
  std::vector<std::string> sourceKeys;
};
