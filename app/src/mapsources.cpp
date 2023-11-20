#include "mapsources.h"
#include "mapsapp.h"
#include "util.h"
#include "scene/scene.h"
#include "style/style.h"  // for making uniforms avail as GUI variables
#include "data/mbtilesDataSource.h"
#include "data/networkDataSource.h"

#include "usvg/svgpainter.h"
#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "mapwidgets.h"
#include "nfd.hpp"  // file dialogs

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
  // skip layer if already added
  for(auto& k : layerkeys) { if(k == key) return; }
  YAML::Node src = sources[key];
  if(!src) {
    LOGE("Invalid map source %s", key.c_str());
    return;
  }
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

  for(const auto& update : src["updates"])
    updates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
  // raster/vector only updates
  const char* updkey = sources[layerkeys[0]]["type"].Scalar() == "Raster" ? "updates_raster" : "updates_vector";
  for(const auto& update : src[updkey])
    updates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
}

std::string SourceBuilder::getSceneYaml(const std::string& baseUrl)
{
  // we'll probably want to skip curl for reading from filesystem in scene/importer.cpp - see tests/src/mockPlatform.cpp
  // or maybe add a Url getParent() method to Url class
  std::string importstr;
  for(auto& url : imports)
    importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
  for(auto& imp : MapsApp::config["sources"]["common_imports"]) {
    std::string url = imp.Scalar();
    importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
  }
  if(importstr.empty())
    return "global:\n\nsources:\n\nlayers:\n";
  return "import:\n" + importstr;  //+ "\nglobal:\n\nsources:\n\nstyles:\n\nlayers:\n";
}

// auto it = mapSources.begin();  std::advance(it, currSrcIdx[ii]-1); builder.addLayer(it->first.Scalar(), it->second);

MapsSources::MapsSources(MapsApp* _app) : MapsComponent(_app)  // const std::string& sourcesFile
{
  FSPath path = FSPath(app->configFile).parent();
  baseUrl = "file://" + path.path;

  FSPath srcfile = path.child(app->config["sources"]["file"].as<std::string>("mapsources.yaml"));
  try {
    mapSources = YAML::LoadFile(srcfile.c_str());
  } catch (...) {
    try {
      mapSources = YAML::LoadFile(srcfile.parent().childPath(srcfile.baseName() + ".default.yaml"));
    } catch (...) {
      LOGE("Unable to load map sources!");
      return;
    }
  }
  srcFile = srcfile.c_str();
}

// don't run this during offline map download!
int64_t MapsSources::shrinkCache(int64_t maxbytes)
{
  std::vector< std::unique_ptr<Tangram::MBTilesDataSource> > dbsources;
  std::vector< std::pair<int, int> > tiles;
  int totalTiles = 0;
  auto insertTile = [&](int offline_id, int t, int size){ ++totalTiles; if(!offline_id) tiles.emplace_back(t, size); };

  FSPath cachedir(app->baseDir, "cache");
  for(auto& file : lsDirectory(cachedir)) {
    FSPath cachefile = cachedir.child(file);
    if(cachefile.extension() != "mbtiles") continue;
    dbsources.push_back(std::make_unique<Tangram::MBTilesDataSource>(
        *app->platform, cachefile.baseName(), cachefile.path, "", true));
    int ntiles = totalTiles;
    dbsources.back()->getTileSizes(insertTile);
    // delete empty cache file
    if(totalTiles == ntiles) {
      LOG("Deleting empty cache file %s", cachefile.c_str());
      dbsources.pop_back();
      removeFile(cachefile.path);
    }
  }

  std::sort(tiles.rbegin(), tiles.rend());  // sort by timestamp, descending (newest to oldest)
  int64_t tot = 0;
  for(auto& x : tiles) {
    tot += x.second;
    if(tot > maxbytes) {
      for(auto& src : dbsources)
        src->deleteOldTiles(x.first);
      break;
    }
  }
  return tot;
}

void MapsSources::addSource(const std::string& key, YAML::Node srcnode)
{
  mapSources[key] = srcnode;
  mapSources[key]["__plugin"] = true;
  //for(auto& k : layerkeys) -- TODO: if modified layer is in use, reload
}

void MapsSources::saveSources()
{
  if(srcFile.empty()) return;
  YAML::Node sources = YAML::Node(YAML::NodeType::Map);
  for(auto& node : mapSources) {
    if(!node.second["__plugin"])
      sources[node.first] = node.second;
  }

  YAML::Emitter emitter;
  //emitter.SetStringFormat(YAML::DoubleQuoted);
  emitter << sources;
  FileStream fs(srcFile.c_str(), "wb");
  fs.write(emitter.c_str(), emitter.size());
}

void MapsSources::sourceModified()
{
  saveBtn->setEnabled(!titleEdit->text().empty());
}

void MapsSources::rebuildSource(const std::string& srcname, bool async)
{
  SourceBuilder builder(mapSources);
  // support comma separated list of sources
  if(!srcname.empty()) {
    currLayers.clear();
    currUpdates.clear();
    auto splitsrc = splitStr<std::vector>(srcname, ",");
    if(splitsrc.size() > 1) {
      for(auto& src : splitsrc)
        currLayers.push_back(src);  //builder.addLayer(src);
    }
    else {
      auto src = mapSources[srcname];
      if(!src) return;
      if(src["type"].Scalar() == "Multi") {
        for(const auto& layer : src["layers"])
          currLayers.push_back(layer["source"].Scalar());
        for(const auto& update : src["updates"])
          currUpdates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
      }
      else
        currLayers.push_back(srcname);
    }
  }

  builder.updates = currUpdates;
  for(auto& src : currLayers)
    builder.addLayer(src);

  if(!builder.imports.empty() || !builder.updates.empty()) {
    // we need this to be persistent for scene reloading (e.g., on scene variable change)
    app->sceneYaml = builder.getSceneYaml(baseUrl);
    app->sceneFile = baseUrl + "__GUI_SOURCES__";
    app->sceneUpdates = std::move(builder.updates);  //.clear();
    app->sceneUpdates.push_back(SceneUpdate{"global.metric_units", app->metricUnits ? "true" : "false"});
    app->loadSceneFile(async);
    sceneVarsLoaded = false;
    legendsLoaded = false;
    currSource = srcname;
    if(!srcname.empty()) {
      app->config["sources"]["last_source"] = currSource;
      for(Widget* item : sourcesContent->select(".listitem"))
        static_cast<Button*>(item)->setChecked(item->node->getStringAttr("__sourcekey", "") == currSource);
    }
  }

  saveBtn->setEnabled(srcname.empty());  // for existing source, don't enable saveBtn until edited
}

std::string MapsSources::createSource(std::string savekey, const std::string& yamlStr)
{
  if(savekey.empty() || !mapSources[savekey]) {
    // find available name
    int ii = mapSources.size();
    while(ii < INT_MAX && mapSources[fstring("custom-%d", ii)]) ++ii;
    savekey = fstring("custom-%d", ii);

    mapSources[savekey] = YAML::Node(YAML::NodeType::Map);
    mapSources[savekey]["type"] = "Multi";
  }

  if(!yamlStr.empty()) {
    try {
      mapSources[savekey] = YAML::Load(yamlStr);
    } catch (...) {
      return "";
    }
  }
  else {
    YAML::Node node = mapSources[savekey];
    node["title"] = titleEdit->text();
    if(node["type"].Scalar() == "Multi") {
      YAML::Node layers = node["layers"] = YAML::Node(YAML::NodeType::Sequence);
      for(auto& src : currLayers)
        layers.push_back(YAML::Load("{source: " + src + "}"));
    }
    YAML::Node updates = node["updates"] = YAML::Node(YAML::NodeType::Map);
    for(const SceneUpdate& upd : app->sceneUpdates) {
      // we only want updates from explicit scene var changes
      if(upd.path[0] != '+')
        updates[upd.path] = upd.value;
    }
  }

  saveSources();
  populateSources();
  rebuildSource(savekey);  // populateSources() resets the layer select boxes, need to restore!
  return savekey;
}

void MapsSources::populateSources()
{
  sourcesDirty = false;
  std::vector<std::string> order = sourcesContent->getOrder();
  if(order.empty()) {
    for(const auto& key : app->config["sources"]["list_order"])
      order.push_back(key.Scalar());
  }
  sourcesContent->clear();

  std::vector<std::string> layerTitles = {};  //"None"
  std::vector<std::string> sourceTitles = {};
  layerKeys = {};  // used for selectLayerDialog
  sourceKeys = {};  // currently only used for quick menu
  for(const auto& src : mapSources) {
    std::string key = src.first.Scalar();
    bool isLayer = src.second["layer"].as<bool>(false) || src.second["type"].Scalar() == "Update";
    if(!isLayer) {
      sourceKeys.push_back(key);
      sourceTitles.push_back(src.second["title"].Scalar());
    }
    if(src.second["type"].Scalar() != "Multi" || isLayer) {
      layerKeys.push_back(key);
      layerTitles.push_back(src.second["title"].Scalar());
    }

    Button* item = createListItem(MapsApp::uiIcon("layers"), src.second["title"].Scalar().c_str());
    item->node->setAttr("__sourcekey", key.c_str());
    item->setChecked(key == currSource);
    Widget* container = item->selectFirst(".child-container");

    Button* editBtn = createToolbutton(MapsApp::uiIcon("edit"), "Show");

    if(isLayer) {
      Button* showBtn = createToolbutton(MapsApp::uiIcon("eye"), "Show");
      showBtn->onClicked = [=](){
        if(key == currSource) return;
        bool show = !showBtn->isChecked();
        showBtn->setChecked(show);
        if(show)
          currLayers.push_back(key);
        else
          currLayers.erase(std::remove(currLayers.begin(), currLayers.end(), key), currLayers.end());
        rebuildSource();  //currLayers
      };
      container->addWidget(showBtn);
      item->onClicked = [=](){
        showBtn->setChecked(false);
        if(key != currSource)
          rebuildSource(key);
      };
      editBtn->onClicked = [=](){ populateSourceEdit(showBtn->isChecked() ? "" : key); };
    }
    else {
      item->onClicked = [=](){ if(key != currSource) rebuildSource(key); };
      editBtn->onClicked = [=](){ populateSourceEdit(key); };
    }

    Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowBtn->setMenu(overflowMenu);
    auto deleteSrcFn = [=](std::string res){
      if(res != "OK") return;
      mapSources.remove(key);
      saveSources();
      app->gui->deleteWidget(item);  //populateSources();
    };
    overflowMenu->addItem("Delete", [=](){
      std::vector<std::string> dependents;
      for (const auto& ssrc : mapSources) {
        for (const auto& layer : ssrc.second["layers"]) {
          if(layer["source"].Scalar() == key)
            dependents.push_back(ssrc.second["title"].Scalar());
        }
      }
      if(!dependents.empty())
        MapsApp::messageBox("Delete source", fstring("%s is used by other sources: %s. Delete anyway?",
            mapSources[key]["title"].Scalar().c_str(), joinStr(dependents, ", ").c_str()),
            {"OK", "Cancel"}, deleteSrcFn);
      else
        deleteSrcFn("OK");
    });

    container->addWidget(editBtn);
    container->addWidget(overflowBtn);
    sourcesContent->addItem(key, item);  //addWidget(item);
  }
  sourcesContent->setOrder(order);

  if(!selectLayerDialog) {
    selectLayerDialog.reset(createSelectDialog("Choose Layer", MapsApp::uiIcon("layers")));
    selectLayerDialog->onSelected = [this](int idx){
      currLayers.push_back(layerKeys[idx]);
      rebuildSource();  //currLayers);
      populateSourceEdit("");
    };
  }
  selectLayerDialog->addItems(layerTitles);
}

void MapsSources::onMapEvent(MapEvent_t event)
{
  if(event == SUSPEND) {
    std::vector<std::string> order = sourcesContent->getOrder();
    if(order.empty()) return;
    YAML::Node ordercfg = app->config["sources"]["list_order"] = YAML::Node(YAML::NodeType::Sequence);
    for(const std::string& s : order)
      ordercfg.push_back(s);
    return;
  }

  if(event != MAP_CHANGE)
    return;
  if(!sceneVarsLoaded && app->map->getScene()->isReady() && sourceEditPanel->isVisible())
    populateSceneVars();

  if(!legendsLoaded && app->map->getScene()->isReady()) {
    legendsLoaded = true;
    // load legend widgets
    app->gui->deleteContents(legendMenu->selectFirst(".child-container"));
    app->gui->deleteContents(app->legendContainer);
    YAML::Node legends = app->readSceneValue("global.__legend");
    for(const auto& legend : legends) {
      Widget* widget = new Widget(loadSVGFragment(legend.second["svg"].Scalar().c_str()));
      app->legendContainer->addWidget(widget);
      widget->setMargins(10, 0, 10, 0);
      widget->setVisible(false);

      Button* menuitem = createCheckBoxMenuItem(legend.second["title"].Scalar().c_str());
      menuitem->onClicked = [=](){
        widget->setVisible(!widget->isVisible());
        menuitem->setChecked(widget->isVisible());
      };
      legendMenu->addItem(menuitem);
    }
    legendBtn->setVisible(app->legendContainer->containerNode()->firstChild() != NULL);
  }
}

void MapsSources::updateSceneVar(const std::string& path, const std::string& newval, bool reload)
{
  app->sceneUpdates.erase(std::remove_if(app->sceneUpdates.begin(), app->sceneUpdates.end(),
      [&](const SceneUpdate& s){ return s.path == path; }), app->sceneUpdates.end());
  app->sceneUpdates.push_back(SceneUpdate{path, newval});
  sourceModified();
  if(reload) {
    app->loadSceneFile();
    sceneVarsLoaded = false;
  }
  else
    app->map->updateGlobals({app->sceneUpdates.back()});
}

Widget* MapsSources::processUniformVar(const std::string& stylename, const std::string& name)
{
  auto& styles = app->map->getScene()->styles();
  for(auto& style : styles) {
    if(style->getName() == stylename) {
      for(auto& uniform : style->styleUniforms()) {
        if(uniform.first.name == name) {
          if(uniform.second.is<float>()) {
            auto spinBox = createTextSpinBox(uniform.second.get<float>(), 1, -INFINITY, INFINITY, "%.2f");
            spinBox->onValueChanged = [=, &uniform](real val){
              std::string path = "styles." + stylename + ".shaders.uniforms." + name;
              app->sceneUpdates.erase(std::remove_if(app->sceneUpdates.begin(), app->sceneUpdates.end(),
                  [&](const SceneUpdate& s){ return s.path == path; }), app->sceneUpdates.end());
              app->sceneUpdates.push_back(SceneUpdate{path, std::to_string(val)});
              uniform.second.set<float>(val);
              app->platform->requestRender();
              sourceModified();
            };
            return spinBox;
          }
          LOGE("Cannot set %s.%s: only float uniforms currently supported in gui_variables!", stylename.c_str(), name.c_str());
          return NULL;
        }
      }
      break;
    }
  }
  LOGE("Cannot find style uniform %s.%s referenced in gui_variables!", stylename.c_str(), name.c_str());
  return NULL;
}

void MapsSources::populateSceneVars()
{
  sceneVarsLoaded = true;
  app->gui->deleteContents(varsContent);

  YAML::Node vars = app->readSceneValue("global.gui_variables");
  for(const auto& var : vars) {
    std::string name = var.first.Scalar();  //.as<std::string>("");
    std::string label = var.second["label"].as<std::string>("");
    std::string stylename = var.second["style"].as<std::string>("");
    bool reload = var.second["reload"].as<std::string>("") != "false";
    if(!stylename.empty()) {  // shader uniform
      Widget* uwidget = processUniformVar(stylename, name);
      if(uwidget)
        varsContent->addWidget(createTitledRow(label.c_str(), uwidget));
    }
    else {  // global variable
      std::string value = app->readSceneValue("global." + name).as<std::string>("");
      if(value == "true" || value == "false") {
        auto checkbox = createCheckBox("", value == "true");
        checkbox->onToggled = [=](bool newval){
          updateSceneVar("global." + name, newval ? "true" : "false", reload);
        };
        varsContent->addWidget(createTitledRow(label.c_str(), checkbox));
      }
      else {
        auto textedit = createTitledTextEdit(label.c_str(), value.c_str());
        textedit->addHandler([=](SvgGui* gui, SDL_Event* event){
          if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_RETURN) {
            updateSceneVar("global." + name, textedit->text(), reload);
            return true;
          }
          return false;
        });
        varsContent->addWidget(textedit);
      }
    }
  }
}

void MapsSources::populateSourceEdit(std::string key)
{
  if(currSource != key)
    rebuildSource(key);

  titleEdit->setText(mapSources[key]["title"].Scalar().c_str());
  app->showPanel(sourceEditPanel, true);
  //sourceEditPanel->selectFirst(".panel-title")->setText(mapSources[key]["title"].Scalar().c_str());
  app->gui->deleteContents(layersContent);

  for(auto& src : currLayers) {
    Button* item = createListItem(MapsApp::uiIcon("layers"), mapSources[src]["title"].Scalar().c_str());
    Widget* container = item->selectFirst(".child-container");

    //<use class="icon elevation-icon" width="18" height="18" xlink:href=":/ui-icons.svg#mountain"/>
    //widgetNode("#listitem-icon")
    //TextEdit* opacityEdit = createTextEdit(80);
    //container->addWidget(opacityEdit);
    //... updates.emplace_back("+layers." + rasterN + ".draw.group-0.alpha", <alpha value>);

    Button* discardBtn = createToolbutton(MapsApp::uiIcon("discard"), "Remove");
    discardBtn->onClicked = [=](){
      currLayers.erase(std::remove(currLayers.begin(), currLayers.end(), src), currLayers.end());
      rebuildSource();  //tempLayers);
      app->gui->deleteWidget(item);
    };
    container->addWidget(discardBtn);
    layersContent->addWidget(item);
  }

  Button* item = createListItem(MapsApp::uiIcon("add"), "Add Layer...");
  item->onClicked = [=](){ showModalCentered(selectLayerDialog.get(), MapsApp::gui); };
  layersContent->addWidget(item);

  if(app->map->getScene()->isReady())
    populateSceneVars();
}

void MapsSources::importSources(const std::string& src)
{
  std::string key;
  if(src.back() == '}') {
    key = createSource("", src);
  }
  else if(Tangram::NetworkDataSource::urlHasTilePattern(src)) {
    key = createSource("", fstring("{type: Raster, title: 'New Source', url: %s}", src.c_str()));
  }
  else {
    // source name conflicts: skip, replace, rename, or cancel? dialog on first conflict?
    app->platform->startUrlRequest(Url(src), [=](UrlResponse&& response){ MapsApp::runOnMainThread( [=](){
      if(response.error)
        MapsApp::messageBox("Import error", fstring("Unable to load '%s': %s", src.c_str(), response.error));
      else {
        try {
          YAML::Node newsources = YAML::Load(response.content.data(), response.content.size());
          for(auto& node : newsources)
            mapSources[node.first.Scalar()] = node.second;
        } catch (std::exception& e) {
          MapsApp::messageBox("Import error", fstring("Error parsing '%s': %s", src.c_str(), e.what()));
        }
      }
    } ); });
    return;
  }
  if(key.empty())
    MapsApp::messageBox("Import error", fstring("Unable to create source from '%s'", src.c_str()));
  else
    populateSourceEdit(key);  // so user can edit title
}

Button* MapsSources::createPanel()
{
  Toolbar* sourceTb = createToolbar();
  titleEdit = createTitledTextEdit("Title");
  titleEdit->node->setAttribute("box-anchor", "hfill");
  saveBtn = createToolbutton(MapsApp::uiIcon("save"), "Save Source");
  //discardBtn = createToolbutton(MapsApp::uiIcon("discard"), "Delete Source");
  sourceTb->node->setAttribute("margin", "0 3");
  sourceTb->addWidget(titleEdit);
  sourceTb->addWidget(saveBtn);

  Toolbar* importTb = createToolbar();
  TextEdit* importEdit = createTitledTextEdit("URL or YAML");
  Button* importFileBtn = createToolbutton(MapsApp::uiIcon("open-folder"), "Open file...");
  Button* importAccept = createToolbutton(MapsApp::uiIcon("accept"), "Save");
  Button* importCancel = createToolbutton(MapsApp::uiIcon("cancel"), "Cancel");
  importTb->addWidget(importFileBtn);
  importTb->addWidget(importEdit);
  importTb->addWidget(importAccept);
  importTb->addWidget(importCancel);
  importTb->setVisible(false);
  importCancel->onClicked = [=](){ importTb->setVisible(false); };

  importFileBtn->onClicked = [=](){
    nfdchar_t* outPath;
    nfdfilteritem_t filterItem[1] = { { "YAML files", "yaml,yml" } };
    nfdresult_t result = NFD_OpenDialog(&outPath, filterItem, 1, NULL);
    if(result != NFD_OKAY)
      return;
    importSources(outPath);
    importTb->setVisible(false);
  };

  // JSON (YAML flow), tile URL, or path/URL to file
  importAccept->onClicked = [=](){
    importSources(importEdit->text());
    importTb->setVisible(false);
  };

  Button* createBtn = createToolbutton(MapsApp::uiIcon("add"), "New Source");
  createBtn->onClicked = [=](){
    // ensure at least the first two layer selects are visible
    //layerRows[0]->setVisible(true);
    //layerRows[1]->setVisible(true);
    currSource = "";
    populateSourceEdit("");  // so user can edit title
  };

  saveBtn->onClicked = [=](){
    createSource(currSource);
    saveBtn->setEnabled(false);
  };

  // we should check for conflicting w/ title of other source here
  titleEdit->onChanged = [this](const char* s){ saveBtn->setEnabled(s[0]); };

  sourcesContent = new DragDropList;
  Widget* sourcesContainer = createColumn();
  sourcesContainer->node->setAttribute("box-anchor", "fill");
  sourcesContainer->addWidget(importTb);
  sourcesContainer->addWidget(sourcesContent);

  Widget* srcEditContent = createColumn();
  varsContent = createColumn();
  varsContent->node->setAttribute("box-anchor", "hfill");
  varsContent->node->setAttribute("margin", "0 3");
  layersContent = createColumn();
  layersContent->node->setAttribute("box-anchor", "hfill");
  srcEditContent->addWidget(varsContent);
  srcEditContent->addWidget(layersContent);

  auto clearCacheFn = [this](std::string res){
    if(res == "OK") {
      shrinkCache(20'000'000);  // 20MB just to test shrinkCache code
      app->storageTotal = app->storageOffline;
    }
  };

  Widget* offlineBtn = app->mapsOffline->createPanel();

  legendBtn = createToolbutton(MapsApp::uiIcon("map-question"), "Legends");
  legendMenu = createMenu(Menu::VERT_LEFT);
  legendBtn->setMenu(legendMenu);
  legendBtn->setVisible(false);

  Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
  Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
  overflowBtn->setMenu(overflowMenu);
  overflowMenu->addItem("Import source", [=](){
    importTb->setVisible(true);
    importEdit->setText("");
    app->gui->setFocused(importEdit);
  });
  overflowMenu->addItem("Clear cache", [=](){
    MapsApp::messageBox("Clear cache", "Delete all cached map data? This action cannot be undone.",
        {"OK", "Cancel"}, clearCacheFn);
  });
  overflowMenu->addItem("Restore default sources", [=](){
    FSPath path = FSPath(app->configFile).parent().child("mapsources.default.yaml");
    importSources(path.path);
  });

  auto sourcesHeader = app->createPanelHeader(MapsApp::uiIcon("layers"), "Map Source");
  sourcesHeader->addWidget(createBtn);
  sourcesHeader->addWidget(legendBtn);
  sourcesHeader->addWidget(offlineBtn);
  sourcesHeader->addWidget(overflowBtn);
  sourcesPanel = app->createMapPanel(sourcesHeader, NULL, sourcesContainer, false);

  sourcesPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      if(sourcesDirty)
        populateSources();
    }
    return false;
  });

  auto editHeader = app->createPanelHeader(MapsApp::uiIcon("edit"), "Edit Source");
  sourceEditPanel = app->createMapPanel(editHeader, srcEditContent, sourceTb);

  // main toolbar button
  Menu* sourcesMenu = createMenu(Menu::VERT_LEFT | (PLATFORM_MOBILE ? Menu::ABOVE : 0));
  //sourcesMenu->autoClose = true;
  sourcesMenu->addHandler([this, sourcesMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(sourcesMenu->selectFirst(".child-container"));
      int uiWidth = app->getPanelWidth();
      if(sourceKeys.empty()) populateSources();
      for(int ii = 0; ii < 10 && ii < sourceKeys.size(); ++ii) {
        std::string key = sourceKeys[ii];
        Button* item = sourcesMenu->addItem(mapSources[key]["title"].Scalar().c_str(),
            MapsApp::uiIcon("layers"), [this, key](){ rebuildSource(key); });
        SvgPainter::elideText(static_cast<SvgText*>(item->selectFirst(".title")->node), uiWidth - 100);
      }
    }
    return false;
  });

  Button* sourcesBtn = app->createPanelButton(MapsApp::uiIcon("layers"), "Sources", sourcesPanel);
  sourcesBtn->setMenu(sourcesMenu);
  return sourcesBtn;
}
