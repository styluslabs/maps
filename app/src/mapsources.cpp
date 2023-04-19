#include "mapsources.h"
#include "mapsapp.h"
#include "util.h"
//#include "imgui.h"
//#include "imgui_stl.h"
#include "scene/scene.h"
#include "style/style.h"  // for making uniforms avail as GUI variables

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"

#include "offlinemaps.h"

// Source selection

class SourceBuilder
{
public:
  const YAML::Node& sources;
  std::vector<std::string> imports;
  std::vector<SceneUpdate> updates;
  int order = 0;

  std::vector<std::string> layerkeys;

  SourceBuilder(const YAML::Node& s) : sources(s) {}

  void addLayer(const std::string& key);
  std::string getSceneYaml(const std::string& baseUrl);
};

void SourceBuilder::addLayer(const std::string& key)  //, const YAML::Node& src)
{
  YAML::Node src = sources[key];
  if(src["type"].Scalar() == "Multi") {
    for (const auto& layer : src["layers"]) {
      std::string layerkey = layer["source"].Scalar();
      addLayer(layerkey);  //, sources[layerkey]);
    }
  }
  else if(src["type"].Scalar() == "Raster") {
    layerkeys.push_back(key);
    std::string rasterN = fstring("raster-%d", order);
    for (const auto& attr : src) {
      if(attr.first.Scalar() != "title")
        updates.emplace_back("+sources." + rasterN + "." + attr.first.Scalar(), yamlToStr(attr.second));
    }
    // if cache file is not explicitly specified, use key since it is guaranteed to be unique
    if(!src["cache"] || src["cache"].Scalar() != "false")
      updates.emplace_back("+sources." + rasterN + ".cache", key);
    // separate style is required for each overlay layer; overlay layers are always drawn over opaque layers
    //  text and points are drawn as overlays w/ blend_order -1, so use blend_order < -1 to place rasters
    //  under vector map text
    if(order > 0)
      updates.emplace_back("+styles." + rasterN, fstring("{base: raster, blend: overlay, blend_order: %d}", order-10));
    updates.emplace_back("+layers." + rasterN + ".data.source", rasterN);
    // order is ignored (and may not be required) for raster styles
    updates.emplace_back("+layers." + rasterN + ".draw.group-0.style", order > 0 ? rasterN : "raster");
    updates.emplace_back("+layers." + rasterN + ".draw.group-0.order", std::to_string(order++));
  }
  else if(src["type"].Scalar() == "Vector") {  // vector map
    imports.push_back(src["url"].Scalar());
    layerkeys.push_back(key);
    ++order;  //order = 9001;  // subsequent rasters should be drawn on top of the vector map
  }
  else if(src["type"].Scalar() == "Update") {
    layerkeys.push_back(key);
  }
  else {
    LOGE("Invalid map source type %s for %s", src["type"].Scalar().c_str(), key.c_str());
    return;
  }

  for(const auto& update : src["updates"]) {
    updates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
  }
}

std::string SourceBuilder::getSceneYaml(const std::string& baseUrl)
{
  // main.cpp prepends file://<cwd>/ to sceneFile!
  // we'll probably want to skip curl for reading from filesystem in scene/importer.cpp - see tests/src/mockPlatform.cpp
  // or maybe add a Url getParent() method to Url class
  if(imports.empty())
    return "global:\n\nsources:\n\nlayers:\n";

  std::string importstr;
  for(auto& url : imports)
    importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
  return "import:\n" + importstr;  //+ "\nglobal:\n\nsources:\n\nstyles:\n\nlayers:\n";
}

// auto it = mapSources.begin();  std::advance(it, currSrcIdx[ii]-1); builder.addLayer(it->first.Scalar(), it->second);

MapsSources::MapsSources(MapsApp* _app) : MapsComponent(_app)  // const std::string& sourcesFile
{
  FSPath path = FSPath(app->configFile).parent();
  std::string srcyaml;
  for(auto srcfile : app->config["sources"]) {
    FSPath srcpath = path.child(srcfile.Scalar());
    // how to handle baseUrl if sources files are in different folders?
    if(baseUrl.empty())
      baseUrl = "file://" + srcpath.parentPath();
    if(!readFile(&srcyaml, srcpath.c_str()))
      LOGW("Unable to open map sources file %s", srcpath.c_str());
    srcyaml += "\n";  // to handle source files w/o trailing newline
  }
  mapSources = YAML::Load(srcyaml.c_str());

  /*
  // have to use Url request to access assets on Android
  auto cb = [&, sourcesFile](UrlResponse&& response) {
    if(response.error)
      LOGE("Unable to load '%s': %s", sourcesFile.c_str(), response.error);
    else {
      try {
        std::lock_guard<std::mutex> lock(sourcesMutex);
        YAML::Node oldsources = Clone(mapSources);
        mapSources = YAML::Load(response.content.data(), response.content.size());
        for(auto& node : oldsources)
          mapSources[node.first.Scalar()] = node.second;
        sourcesLoaded = true;
      } catch (std::exception& e) {
        LOGE("Error parsing '%s': %s", sourcesFile.c_str(), e.what());
      }
    }
  };

  app->platform->startUrlRequest(Url(sourcesFile), cb);

  std::size_t sep = sourcesFile.find_last_of("/\\");
  if(sep != std::string::npos)
    baseUrl = sourcesFile.substr(0, sep+1);  //"file://" +
  */
}

void MapsSources::addSource(const std::string& key, YAML::Node srcnode)
{
  std::lock_guard<std::mutex> lock(sourcesMutex);
  mapSources[key] = srcnode;
  //for(auto& k : layerkeys) -- TODO: if modified layer is in use, reload
}

/*void MapsSources::showGUI()
{
  static constexpr int MAX_SOURCES = 8;
  static int currIdx = 0;
  static std::vector<int> currSrcIdx(MAX_SOURCES, 0);
  static int nSources = 1;
  static std::string newSrcTitle;

  if (!ImGui::CollapsingHeader("Sources", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  if(!sourcesLoaded) {
    ImGui::TextWrapped("Loading mapsources.yaml...");
    return;
  }

  try {

  std::vector<std::string> titles = {"None"};
  std::vector<std::string> keys = {""};
  for (const auto& src : mapSources) {
    titles.push_back(src.second["title"].Scalar());
    keys.push_back(src.first.Scalar());
  }

  std::vector<const char*> ctitles;
  for(const auto& s : titles)
    ctitles.push_back(s.c_str());

  int reload = 0;
  if(ImGui::Combo("Source", &currIdx, ctitles.data(), ctitles.size()))
    reload = 1;  // selected source changed - reload scene

  if(currIdx > 0 && mapSources[keys[currIdx]]["type"].Scalar() == "Multi") {
    ImGui::SameLine();
    if (ImGui::Button("Remove"))
      mapSources.remove(keys[currIdx]);
  }
  ImGui::Separator();
  for(int ii = 0; ii < nSources; ++ii) {
    if(ImGui::Combo(fstring("Layer %d", ii+1).c_str(), &currSrcIdx[ii], ctitles.data(), ctitles.size()))
      reload = 2;  // layer changed - reload scene
  }

  if (nSources > 1) {
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
      --nSources;
      if(currSrcIdx[nSources] > 0)
        reload = 2;
    }
  }
  if (nSources < MAX_SOURCES && ImGui::Button("Add Layer")) {
    currSrcIdx[nSources] = 0;
    ++nSources;
  }

  if(reload) {
    SourceBuilder builder(mapSources);
    if(reload == 1)
      builder.addLayer(keys[currIdx]);
    else {
      for(int ii = 0; ii < nSources; ++ii) {
        if(currSrcIdx[ii] > 0)
          builder.addLayer(keys[currSrcIdx[ii]]);
      }
    }

    if(!builder.imports.empty() || !builder.updates.empty()) {
      app->sceneYaml = builder.getSceneYaml(baseUrl);
      app->sceneFile = baseUrl + "__GUI_SOURCES__";
      app->loadSceneFile(false, builder.updates);
    }

    if(reload == 1 && builder.layerkeys.size() > 1)
      newSrcTitle = titles[currIdx];

    nSources = std::max(int(builder.layerkeys.size()), nSources);
    for(size_t ii = 0; ii < builder.layerkeys.size(); ++ii) {
      for(int jj = 0; jj < keys.size(); ++jj) {
        if(builder.layerkeys[ii] == keys[jj]) {
          currSrcIdx[ii] = jj;
          break;  // next layer
        }
      }
    }
    for(int ii = builder.layerkeys.size(); ii < nSources; ++ii)
      currSrcIdx[ii] = 0;
  }

  if(nSources > 1) {
    ImGui::InputText("Name", &newSrcTitle, ImGuiInputTextFlags_EnterReturnsTrue);
    //ent = ImGui::Button("Save") || ent;
    if(ImGui::Button("Save") && !newSrcTitle.empty()) {
      std::stringstream fs;  //fs(sourcesFile, std::fstream::app | std::fstream::binary);

      // find available name
      std::string savekey;
      if(currIdx > 0 && newSrcTitle == titles[currIdx] && mapSources[keys[currIdx]]["type"].Scalar() == "Multi")
        savekey = keys[currIdx];
      else {
        int ii = mapSources.size();
        while(ii < INT_MAX && mapSources[fstring("custom-%d", ii)]) ++ii;
        savekey = fstring("custom-%d", ii);
        currIdx = keys.size();  // new source will be added at end of list
      }
      //YAML::Node node = mapSources[savekey] = YAML::Node(YAML::NodeType::Map);  node["type"] = "Multi";
      fs << "type: Multi\n";
      fs << "title: " << newSrcTitle << "\n";
      fs << "layers:\n";
      for(int ii = 0; ii < nSources; ++ii) {
        if(currSrcIdx[ii] > 0)
          fs << "  - source: " << keys[currSrcIdx[ii]] << "\n";
      }
      newSrcTitle.clear();
      mapSources[savekey] = YAML::Load(fs.str());
      // we'd set a flag here to save mapsources.yaml on exit
    }
  }

  } catch (std::exception& e) {
    ImGui::TextWrapped("Error parsing mapsources.yaml: %s", e.what());
  }
}*/

// New GUI

constexpr int MAX_SOURCES = 8;
//std::vector<int> currSrcIdx(MAX_SOURCES, 0);

void MapsSources::rebuildSource(const std::string& srcname)
{
  SourceBuilder builder(mapSources);
  if(!srcname.empty())
    builder.addLayer(srcname);
  else {
    for(int ii = 0; ii < nSources; ++ii) {
      int idx = layerCombos[ii]->index();
      if(idx > 0)
        builder.addLayer(layerKeys[idx]);
    }
  }

  if(!builder.imports.empty() || !builder.updates.empty()) {
    // we need this to be persistent for scene reloading (e.g., on scene variable change)
    app->sceneYaml = builder.getSceneYaml(baseUrl);
    app->sceneFile = baseUrl + "__GUI_SOURCES__";
    app->sceneUpdates = std::move(builder.updates);  //.clear();
    app->loadSceneFile();  //builder.getSceneYaml(baseUrl), builder.updates);
    if(!srcname.empty())
      app->config["last_source"] = srcname;
  }

  bool multi = builder.layerkeys.size() > 1;
  discardBtn->setEnabled(!srcname.empty() && multi);
  saveBtn->setEnabled(srcname.empty() && multi);

  size_t ii = 0;
  nSources = std::max(int(builder.layerkeys.size()), nSources);
  for(; ii < builder.layerkeys.size(); ++ii) {
    for(int jj = 0; jj < sourceKeys.size(); ++jj) {
      if(builder.layerkeys[ii] == sourceKeys[jj]) {
        //currSrcIdx[ii] = jj;
        layerCombos[ii]->setIndex(jj);
        layerRows[ii]->setVisible(true);
        break;  // next layer
      }
    }
  }

  layerCombos[ii]->setIndex(0);
  layerRows[ii]->setVisible(true);

  for(++ii; ii < nSources; ++ii)
    layerRows[ii]->setVisible(false);
}

void MapsSources::createSource(std::string savekey, const std::string& newSrcTitle)
{
  std::stringstream fs;  //fs(sourcesFile, std::fstream::app | std::fstream::binary);

  //if(currIdx > 0 && newSrcTitle == titles[currIdx] && mapSources[keys[currIdx]]["type"].Scalar() == "Multi")
  if(savekey.empty()) {
    // find available name
    int ii = mapSources.size();
    while(ii < INT_MAX && mapSources[fstring("custom-%d", ii)]) ++ii;
    savekey = fstring("custom-%d", ii);
    //currIdx = keys.size();  // new source will be added at end of list
  }
  //YAML::Node node = mapSources[savekey] = YAML::Node(YAML::NodeType::Map);  node["type"] = "Multi";
  fs << "type: Multi\n";
  fs << "title: " << newSrcTitle << "\n";
  fs << "layers:\n";
  for(int ii = 0; ii < nSources; ++ii) {
    int idx = layerCombos[ii]->index();
    if(idx > 0)
      fs << "  - source: " << layerKeys[idx] << "\n";
  }
  // scene updates
  if(!app->sceneUpdates.empty())
    fs << "updates:\n";
  for(const SceneUpdate& upd : app->sceneUpdates)
    fs << "  " << upd.path << ": " << upd.value << "\n";
  mapSources[savekey] = YAML::Load(fs.str());
  // we'd set a flag here to save mapsources.yaml on exit

  sourceKeys.push_back(savekey);
  populateSources();  //sourceCombo->addItems({newSrcTitle}); ... doesn't work because of different sorting options
}

#include "data/mbtilesDataSource.h"

void MapsSources::deleteOfflineMap(int mapid)
{
  int64_t offlineSize = 0;
  for(const auto& src : mapSources) {
    std::string cachename = src["cache"] ? src["cache"].Scalar() : src.first.Scalar();
    std::string cachefile = app->baseDir + "cache/" + cachename + ".mbtiles";  //options.diskCacheDir
    if(cachename == "false" || !FSPath(cachefile).exists())  // don't create file if it doesn't exist
      continue;
    auto s = std::make_unique<Tangram::MBTilesDataSource>(*app->platform, src.first.Scalar(), cachefile, "", true);
    offlineSize -= s->getOfflineSize();
    s->deleteOfflineMap(mapid);
    offlineSize += s->getOfflineSize();
  }
  app->platform->notifyStorage(0, offlineSize);  // this can trigger cache shrink, so wait until all sources processed
}

// don't run this during offline map download!
int64_t MapsSources::shrinkCache(int64_t maxbytes)
{
  std::vector< std::unique_ptr<Tangram::MBTilesDataSource> > dbsources;
  std::vector< std::pair<int, int> > tiles;
  auto insertTile = [&](int timestamp, int size){ tiles.emplace_back(timestamp, size); };

  for(const auto& src : mapSources) {
    std::string cachename = src["cache"] ? src["cache"].Scalar() : src.first.Scalar();
    std::string cachefile = app->baseDir + "cache/" + cachename + ".mbtiles";  //options.diskCacheDir
    if(cachename == "false" || !FSPath(cachefile).exists())
      continue;
    dbsources.push_back(std::make_unique<Tangram::MBTilesDataSource>(*app->platform, src.first.Scalar(), cachefile, "", true));
    dbsources.back()->getTileSizes(insertTile);
  }

  std::sort(tiles.rbegin(), tiles.rend());  // sort by timestamp, descending (newest to oldest)
  int64_t tot = 0;
  for(auto& x : tiles) {
    tot += x.second;
    if(tot > maxbytes) {
      for(auto& src : dbsources)
        src->deleteOldTiles(x.first);
    }
  }
  return tot;
}

void MapsSources::populateSources()
{
  std::vector<std::string> layerTitles = {"None"};
  std::vector<std::string> sourceTitles = {};
  sourceKeys = {""};
  layerKeys = {};
  for(const auto& src : mapSources) {
    sourceKeys.push_back(src.first.Scalar());
    sourceTitles.push_back(src.second["title"].Scalar());
    if(src.second["type"].Scalar() != "Multi") {
      layerKeys.push_back(src.first.Scalar());
      layerTitles.push_back(src.second["title"].Scalar());
    }
  }
  sourceCombo->addItems(sourceTitles);
  for(ComboBox* combo : layerCombos)
    combo->addItems(layerTitles);
}

void MapsSources::onSceneLoaded()
{
  populateSceneVars();

  // how to get var values when saving new source? save a SceneUpdate for each var change?
}

void MapsSources::populateSceneVars()
{
  app->gui->deleteContents(varsContent);

  YAML::Node vars = app->readSceneValue("global.gui_variables");
  for(const auto& var : vars) {
    std::string name = var["name"].as<std::string>("");
    std::string label = var["label"].as<std::string>("");
    std::string reload = var["reload"].as<std::string>("");
    std::string stylename = var["style"].as<std::string>("");
    if(!stylename.empty()) {
      // shader uniform
      auto& styles = app->map->getScene()->styles();
      for(auto& style : styles) {
        if(style->getName() == stylename) {
          for(auto& uniform : style->styleUniforms()) {
            if(uniform.first.name == name) {
              if(uniform.second.is<float>()) {
                auto spinBox = createTextSpinBox(uniform.second.get<float>());
                spinBox->onValueChanged = [=, &uniform](real val){
                  app->sceneUpdates.push_back(SceneUpdate{"styles." + stylename + ".shaders.uniforms." + name, std::to_string(val)});
                  uniform.second.set<float>(val);
                };
                varsContent->addWidget(createTitledRow(label.c_str(), spinBox));
              }
              else
                LOGE("Cannot set %s.%s: only float uniforms currently supported in gui_variables!", stylename.c_str(), name.c_str());
              return;
            }
          }
          break;
        }
      }
      LOGE("Cannot find style uniform %s.%s referenced in gui_variables!", stylename.c_str(), name.c_str());
    }
    else {
      // global variable, accessed in scene file by JS functions
      std::string value = app->readSceneValue("global." + name).as<std::string>("");
      auto checkbox = createCheckBox("", value == "true");
      checkbox->onToggled = [=](bool newval){
        app->sceneUpdates.push_back(SceneUpdate{"global." + name, newval ? "true" : "false"});
        if(reload == "false")  // ... so default to reloading
          app->map->updateGlobals({app->sceneUpdates.back()});  //SceneUpdate{"global." + name, newval ? "true" : "false"}});
        else
          app->loadSceneFile();  //{SceneUpdate{"global." + name, newval ? "true" : "false"}});
      };
      varsContent->addWidget(createTitledRow(label.c_str(), checkbox));
    }
  }
}

Widget* MapsSources::createPanel()
{
  Toolbar* sourceTb = createToolbar();
  sourceCombo = createComboBox({});
  saveBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_save.svg"), "Save Source");
  discardBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_discard.svg"), "Delete Source");
  Button* createBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_plus.svg"), "New Source");
  sourceTb->addWidget(sourceCombo);
  sourceTb->addWidget(discardBtn);
  sourceTb->addWidget(saveBtn);
  sourceTb->addWidget(createBtn);

  Toolbar* newSrcTb = createToolbar();
  TextEdit* newSrcEdit = createTextEdit();
  Button* newSrcAccept = createToolbutton(SvgGui::useFile(":/icons/ic_menu_accept.svg"), "OK");
  Button* newSrcCancel = createToolbutton(SvgGui::useFile(":/icons/ic_menu_cancel.svg"), "Cancel");
  newSrcTb->addWidget(newSrcEdit);
  newSrcTb->addWidget(newSrcAccept);
  newSrcTb->addWidget(newSrcCancel);

  newSrcCancel->onClicked = [=](){
    newSrcTb->setVisible(false);
    sourceTb->setVisible(true);
  };

  newSrcAccept->onClicked = [=](){
    createSource("", newSrcEdit->text());
    newSrcTb->setVisible(false);
    sourceTb->setVisible(true);
  };

  sourceCombo->onChanged = [this](const char*){
    rebuildSource(sourceKeys[sourceCombo->index()]);
  };

  createBtn->onClicked = [=](){
    newSrcEdit->setText("");
    newSrcTb->setVisible(true);
    sourceTb->setVisible(false);
  };

  saveBtn->onClicked = [this](){
    createSource(sourceKeys[sourceCombo->index()], sourceCombo->text());
  };

  discardBtn->onClicked = [this](){
    mapSources.remove(sourceKeys[sourceCombo->index()]);
    app->gui->deleteContents(sourceCombo->selectFirst(".child-container"));
    for(ComboBox* combo : layerCombos)
      app->gui->deleteContents(combo->selectFirst(".child-container"));
    populateSources();
  };

  Widget* sourceRow = createRow();
  sourceRow->addWidget(newSrcTb);
  sourceRow->addWidget(sourceTb);
  newSrcTb->setVisible(false);

  //Widget* sourcesContent = createColumn();
  //sourcesContent->addWidget(createTitledRow("Source", sourceRow));
  Widget* sourcesContent = createTitledRow("Source", sourceRow);

  Widget* layersContent = createColumn();
  varsContent = createColumn();
  layersContent->addWidget(varsContent);
  for(int ii = 0; ii < MAX_SOURCES; ++ii) {
    layerCombos.push_back(createComboBox({}));
    layerCombos.back()->onChanged = [this](const char*){
      rebuildSource();
    };
    layerRows.push_back(createTitledRow(fstring("Layer %d", ii).c_str(), layerCombos.back()));
    layersContent->addWidget(layerRows.back());
  }
  populateSources();

  Widget* offlineBtn = app->mapsOffline->createPanel();

  auto toolbar = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_cloud.svg"), "Map Source");
  toolbar->addWidget(createStretch());
  toolbar->addWidget(offlineBtn);
  sourcesPanel = app->createMapPanel(toolbar, layersContent, sourcesContent);

  // main toolbar button
  Menu* sourcesMenu = createMenu(Menu::VERT_LEFT);
  sourcesMenu->autoClose = true;
  sourcesMenu->addHandler([this, sourcesMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(sourcesMenu->selectFirst(".child-container"));

      for(int ii = 0; ii < 10 && ii < sourceKeys.size(); ++ii) {
        sourcesMenu->addItem(mapSources[sourceKeys[ii]]["title"].Scalar().c_str(),
            [ii, this](){ sourceCombo->updateIndex(ii); });  // rebuildSource();
      }

    }
    return false;
  });

  Button* sourcesBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Sources");
  sourcesBtn->setMenu(sourcesMenu);
  sourcesBtn->onClicked = [this](){
    app->showPanel(sourcesPanel);
  };

  return sourcesBtn;
}
