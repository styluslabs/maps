#pragma once

#include "mapscomponent.h"
#include "gaml/src/yaml.h"

class MapsSources : public MapsComponent
{
  friend class MapsOffline;
public:
  MapsSources(MapsApp* _app);

  void reload();
  void addSource(const std::string& key, YAML::Node srcnode);
  void rebuildSource(const std::string& srcname = "", bool async = true);
  void onMapEvent(MapEvent_t event);
  void updateSceneVar(const std::string& path, const std::string& newval, const std::string& onchange, bool reload);
  void importSources(const std::string& src);
  bool syncImportFile(const std::string& filename);
  Button* createPanel();

  std::string currSource;

private:
  std::string createSource(std::string savekey, const std::string& yamlStr = "");
  void populateSources();
  Widget* processUniformVar(const std::string& stylename, const std::string& name, const YAML::Node& varnode);
  void populateSceneVars();
  void populateSourceEdit(std::string key);
  void sourceModified();
  void saveSources();
  void promptDownload(const std::vector<std::string>& keys);

  Widget* sourcesPanel = NULL;
  Widget* sourceEditPanel = NULL;
  DragDropList* sourcesContent = NULL;
  Widget* archivedPanel = NULL;
  Widget* archivedContent = NULL;
  Widget* layersContent = NULL;
  Button* legendBtn = NULL;
  Menu* legendMenu = NULL;
  TextEdit* titleEdit = NULL;
  TextEdit* urlEdit = NULL;
  Button* saveBtn = NULL;
  Widget* varsContent = NULL;
  Widget* varsSeparator = NULL;
  TextBox* creditsText = NULL;
  std::unique_ptr<SelectDialog> selectLayerDialog;
  std::unique_ptr<Dialog> importDialog;

  struct SourceLayer {
    std::string source; float opacity;
    bool operator==(const std::string& s) { return s == source; }
  };

  std::string baseUrl;
  std::string srcFile;
  YAML::Node mapSources;
  std::vector<SourceLayer> currLayers;
  std::vector<SceneUpdate> currUpdates;
  std::vector<std::string> layerKeys;
  bool legendsLoaded = false;
  bool sourcesDirty = true;
  bool saveSourcesNeeded = false;
  bool sceneVarsLoaded = false;
};
