#pragma once

#include "mapscomponent.h"
#include "yaml-cpp/yaml.h"

class MapsSources : public MapsComponent
{
  friend class MapsOffline;
public:
  MapsSources(MapsApp* _app);

  void addSource(const std::string& key, YAML::Node srcnode);
  void rebuildSource(const std::string& srcname = "", bool async = true);
  int64_t shrinkCache(int64_t maxbytes);
  void onMapEvent(MapEvent_t event);
  Button* createPanel();

  std::string currSource;
  bool sceneVarsLoaded = false;

private:
  std::string createSource(std::string savekey, const std::string& yamlStr = "");
  void populateSources();
  void updateSceneVar(const std::string& path, const std::string& newval, bool reload);
  Widget* processUniformVar(const std::string& stylename, const std::string& name);
  void populateSceneVars();
  void populateSourceEdit(std::string key);
  void sourceModified();
  void saveSources();
  void importSources(const std::string& src);

  Widget* sourcesPanel = NULL;
  Widget* sourceEditPanel = NULL;
  DragDropList* sourcesContent = NULL;
  Widget* archivedPanel = NULL;
  Widget* archivedContent = NULL;
  std::string baseUrl;
  std::string srcFile;
  YAML::Node mapSources;
  std::atomic<bool> sourcesLoaded{false};
  std::vector<std::string> currLayers;
  std::vector<SceneUpdate> currUpdates;

  Widget* layersContent = NULL;
  Button* legendBtn = NULL;
  Menu* legendMenu = NULL;
  TextEdit* titleEdit = NULL;
  Button* saveBtn = NULL;
  Widget* varsContent = NULL;
  bool legendsLoaded = false;
  bool sourcesDirty = true;
  std::vector<std::string> layerKeys;
  std::unique_ptr<SelectDialog> selectLayerDialog;
};
