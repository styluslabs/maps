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

#include "offlinemaps.h"
#include "plugins.h"

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
  if(src["layers"]) {  // multi-layer
    for (const auto& layer : src["layers"]) {
      std::string layerkey = layer["source"].Scalar();
      addLayer(layerkey);  //, sources[layerkey]);
    }
  }
  else if(src["url"]) {  // raster tiles
    layerkeys.push_back(key);
    std::string rasterN = fstring("raster-%d", order);
    updates.emplace_back("+sources." + rasterN + ".type", "Raster");
    for (const auto& attr : src) {
      const std::string& k = attr.first.Scalar();
      if(k != "title" && k != "archived")
        updates.emplace_back("+sources." + rasterN + "." + attr.first.Scalar(), yamlToStr(attr.second));
    }
    // if cache file is not explicitly specified, use key since it is guaranteed to be unique
    if(!src["cache"] || src["cache"].Scalar() != "false")
      updates.emplace_back("+sources." + rasterN + ".cache", key);
    // separate style is required for each overlay layer
    //  use translucent instead of overlay so that depth test can place proxy tiles underneath other tiles
    //  text and points are drawn w/ blend_order -1, so use blend_order < -1 to place rasters underneath
    // note that lines and polygons are normally drawn w/ opaque blend mode, which ignores blend_order and is
    //  drawn before all other blend modes; default raster style uses opaque!
    if(order > 0) {
      updates.emplace_back("+styles." + rasterN,
          fstring("{base: raster, lighting: false, blend: translucent, blend_order: %d}", order-100));
    }
    updates.emplace_back("+layers." + rasterN + ".data.source", rasterN);
    // order is ignored (and may not be required) for raster styles
    updates.emplace_back("+layers." + rasterN + ".draw.group-0.style", order > 0 ? rasterN : "raster");
    //updates.emplace_back("+layers." + rasterN + ".draw.group-0.alpha", alphastr);  -- this is how to do opacity
    updates.emplace_back("+layers." + rasterN + ".draw.group-0.order", order > 0 ? std::to_string(1000 + order) : "0");
    ++order;
  }
  else if(src["scene"]) {  // vector map
    imports.push_back(src["scene"].Scalar());
    layerkeys.push_back(key);
    ++order;  //order = 9001;  // subsequent rasters should be drawn on top of the vector map
  }
  else {  // update only
    layerkeys.push_back(key);
  }

  for(const auto& update : src["updates"])
    updates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
  // raster/vector only updates
  const char* updkey = sources[layerkeys[0]]["url"] ? "updates_raster" : "updates_vector";
  for(const auto& update : src[updkey])
    updates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
}

std::string SourceBuilder::getSceneYaml(const std::string& baseUrl)
{
  // we'll probably want to skip curl for reading from filesystem in scene/importer.cpp - see tests/src/mockPlatform.cpp
  // or maybe add a Url getParent() method to Url class
  std::string importstr;
  // before scene so scene can override things, but we can split into pre_imports and post_imports if needed
  for(auto& imp : MapsApp::config["sources"]["common_imports"]) {
    std::string url = imp.Scalar();
    importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
  }
  for(auto& url : imports)
    importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
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

void MapsSources::addSource(const std::string& key, YAML::Node srcnode)
{
  mapSources[key] = srcnode;
  if(!mapSources[key]["__plugin"])
    saveSourcesNeeded = true;
  if(sourcesPanel && sourcesPanel->isVisible())
    populateSources();
  else
    sourcesDirty = true;
  //for(auto& k : layerkeys) -- TODO: if modified layer is in use, reload
}

void MapsSources::saveSources()
{
  saveSourcesNeeded = false;
  if(srcFile.empty()) return;
  YAML::Node sources = YAML::Node(YAML::NodeType::Map);
  for(auto& node : mapSources) {
    if(!node.second["__plugin"])  // plugin can set this flag for sources which should not be saved
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
      if(src["layers"] && !src["layer"].as<bool>(false)) {
        for(const auto& layer : src["layers"])
          currLayers.push_back(layer["source"].Scalar());
        for(const auto& update : src["updates"])
          currUpdates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
      }
      else
        currLayers.push_back(srcname);
    }
  }

  for(auto& src : currLayers)
    builder.addLayer(src);
  builder.updates.insert(builder.updates.end(), currUpdates.begin(), currUpdates.end());

  if(!builder.imports.empty() || !builder.updates.empty()) {
    // we need this to be persistent for scene reloading (e.g., on scene variable change)
    app->sceneYaml = builder.getSceneYaml(baseUrl);
    app->sceneFile = baseUrl + "__GUI_SOURCES__";
    app->sceneUpdates = std::move(builder.updates);  //.clear();
    app->loadSceneFile(async);
    sceneVarsLoaded = false;
    legendsLoaded = false;
    currSource = srcname;
    if(!srcname.empty())
      app->config["sources"]["last_source"] = currSource;
    auto sourcesItems = sourcesContent->select(".listitem");
    auto archiveItems = archivedContent->select(".listitem");
    sourcesItems.insert(sourcesItems.end(), archiveItems.begin(), archiveItems.end());
    for(Widget* item : sourcesItems) {
      std::string key = item->node->getStringAttr("__sourcekey", "");
      if(key.empty()) continue;
      if(!currSource.empty())
        static_cast<Button*>(item)->setChecked(key == currSource);
      Button* showbtn = static_cast<Button*>(item->selectFirst(".show-btn"));
      if(showbtn) {
        bool shown = std::find(currLayers.begin(), currLayers.end(), key) != currLayers.end()
            || std::find(builder.layerkeys.begin(), builder.layerkeys.end(), key) != builder.layerkeys.end();
        showbtn->setChecked(shown);
      }
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
    mapSources[savekey]["layers"] = YAML::Node(YAML::NodeType::Sequence);  //"Multi";
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
    YAML::Node layers = node["layers"];
    node["title"] = titleEdit->text();
    if(layers) {
      for(auto& src : currLayers)
        layers.push_back(YAML::Load("{source: " + src + "}"));
    }
    YAML::Node updates = node["updates"] = YAML::Node(YAML::NodeType::Map);
    // note that gui var changes will come after any defaults in currUpdates and thus replace them as desired
    for(const SceneUpdate& upd : currUpdates)   //app->sceneUpdates) {
      updates[upd.path[0] == '+' ? upd.path.substr(1) : upd.path] = upd.value;
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
  app->gui->deleteContents(archivedContent, ".listitem");

  std::vector<std::string> layerTitles = {};  //"None"
  layerKeys = {};

  std::vector<std::string> allKeys;
  for(const auto& src : mapSources)
    allKeys.push_back(src.first.Scalar());
  std::sort(allKeys.begin(), allKeys.end(), [&](auto& a, auto& b){
      return mapSources[a]["title"].Scalar() < mapSources[b]["title"].Scalar(); });
  for(const std::string& key : allKeys) {
    auto src = mapSources[key];
    bool archived = src["archived"].as<bool>(false);
    bool isLayer = src["layer"].as<bool>(false);
    if(!src["layers"] || isLayer) {
      layerKeys.push_back(key);
      layerTitles.push_back(src["title"].Scalar());
    }

    Button* item = createListItem(MapsApp::uiIcon("layers"), src["title"].Scalar().c_str());
    item->node->setAttr("__sourcekey", key.c_str());
    item->setChecked(key == currSource);
    Widget* container = item->selectFirst(".child-container");

    if(item->isChecked()) {
      // scroll as needed to show selected source - onApplyLayout is called on parent after children
      sourcesContent->onApplyLayout = [this, item](const Rect&, const Rect&){
        Rect ritem = item->node->bounds();
        Rect rview = sourcesContent->scrollWidget->node->bounds();
        if(ritem.bottom > rview.bottom)
          sourcesContent->scrollWidget->scrollTo({0, ritem.top - rview.top});
        sourcesContent->onApplyLayout = {};  // only once
        return false;  // continue w/ applyLayout normally
      };
    }

    Button* editBtn = createToolbutton(MapsApp::uiIcon("edit"), "Show");
    if(isLayer) {
      Button* showBtn = createToolbutton(MapsApp::uiIcon("eye"), "Show");
      showBtn->node->addClass("show-btn");
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

    overflowMenu->addItem(archived ? "Unarchive" : "Archive", [=](){
      if(archived)
        mapSources[key].remove("archived");
      else
        mapSources[key]["archived"] = true;
      sourcesDirty = true;
      saveSources();
      app->gui->deleteWidget(item);
    });

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
    if(archived)
      archivedContent->addWidget(item);
    else
      sourcesContent->addItem(key, item);  //addWidget(item);
  }
  Button* item = createListItem(MapsApp::uiIcon("archive"), "Archived Sources");
  item->onClicked = [this](){ app->showPanel(archivedPanel, true); if(sourcesDirty) populateSources(); };
  sourcesContent->addItem("archived", item);
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
    if(saveSourcesNeeded)
      saveSources();
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
    // adjust status bar color on mobile
    app->notifyStatusBarBG(!app->readSceneValue("global.dark_base_map").as<bool>(false));
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

static void replaceSceneVar(std::vector<SceneUpdate>& vars, const std::string& path, const std::string& newval)
{
  vars.erase(std::remove_if(vars.begin(), vars.end(),
      [&](const SceneUpdate& s){ return s.path == path; }), vars.end());
  vars.push_back(SceneUpdate{path, newval});
}

void MapsSources::updateSceneVar(const std::string& path, const std::string& newval, const std::string& onchange, bool reload)
{
  replaceSceneVar(app->sceneUpdates, path, newval);
  replaceSceneVar(currUpdates, path, newval);
  sourceModified();

  if(!onchange.empty()) {
    app->map->updateGlobals({app->sceneUpdates.back()});
    app->pluginManager->jsCallFn(onchange.c_str());
    rebuildSource(currSource);  // plugin will update mapSources, so we need to reload completely
  }
  else if(reload) {
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
              std::string newval = std::to_string(val);
              replaceSceneVar(app->sceneUpdates, path, newval);
              replaceSceneVar(currUpdates, path, newval);
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
    std::string onchange = var.second["onchange"].as<std::string>("");
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
          updateSceneVar("global." + name, newval ? "true" : "false", onchange, reload);
        };
        varsContent->addWidget(createTitledRow(label.c_str(), checkbox));
      }
      else if(var.second["choices"]) {
        std::vector<std::string> choices;
        for(const auto& choice : var.second["choices"])
          choices.push_back(choice.Scalar());
        auto combobox = createComboBox(choices);
        combobox->setText(value.c_str());
        combobox->onChanged = [=](const char* val){
          updateSceneVar("global." + name, val, onchange, reload);
        };
        varsContent->addWidget(createTitledRow(label.c_str(), combobox));
      }
      else if(var.second["type"].as<std::string>("") == "date") {
        auto parts = splitStr<std::vector>(value, " -/");
        if(parts.size() != 3) parts = {"2024", "01", "01"};
        int year0 = atoi(parts[0].c_str());
        int month0 = atoi(parts[1].c_str());  // + (parts[1].front() == '0' ? 1 : 0));
        int day0 = atoi(parts[2].c_str());  // + (parts[2].front() == '0' ? 1 : 0));
        auto datepicker = createDatePicker(year0, month0, day0, [=](int year, int month, int day){
          updateSceneVar("global." + name, fstring("%04d-%02d-%02d", year, month, day), onchange, reload);
        });
        varsContent->addWidget(createTitledRow(label.c_str(), datepicker));
      }
      else {
        auto textedit = createTitledTextEdit(label.c_str(), value.c_str());
        textedit->addHandler([=](SvgGui* gui, SDL_Event* event){
          if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_RETURN) {
            updateSceneVar("global." + name, textedit->text(), onchange, reload);
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
  std::string title = mapSources[key] ? mapSources[key]["title"].Scalar() : "Untitled";

  titleEdit->setText(title.c_str());
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
    key = createSource("", fstring("{title: 'New Source', url: %s}", src.c_str()));
  }
  else {
    // source name conflicts: skip, replace, rename, or cancel? dialog on first conflict?
    app->platform->startUrlRequest(Url(src), [=](UrlResponse&& response){ MapsApp::runOnMainThread( [=](){
      if(response.error)
        MapsApp::messageBox("Import error", fstring("Unable to load '%s': %s", src.c_str(), response.error));
      else {
        try {
          YAML::Node yml = YAML::Load(response.content.data(), response.content.size());
          if(yml["global"] || yml["layers"] || yml["styles"] || yml["import"] || yml["sources"])
            createSource("", fstring("{title: '%s', scene: %s}", FSPath(src).baseName().c_str(), src.c_str()));
          else {
            for(auto& node : yml)
              mapSources[node.first.Scalar()] = node.second;
          }
          saveSources();
          populateSources();
        } catch (std::exception& e) {
          MapsApp::messageBox("Import error", fstring("Error parsing '%s': %s", src.c_str(), e.what()));
        }
      }
    } ); });
    return;
  }
  if(key.empty())
    MapsApp::messageBox("Import error", fstring("Unable to create source from '%s'", src.c_str()));
  else {
    saveSources();
    populateSourceEdit(key);  // so user can edit title
    sourcesDirty = true;
  }
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

  auto importFileFn = [=](const char* outPath){
    importSources(std::string("file://") + outPath);
    importTb->setVisible(false);
  };
  importFileBtn->onClicked = [=](){ MapsApp::openFileDialog({{"YAML files", "yaml,yml"}}, importFileFn); };

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

  archivedContent = createColumn();
  auto archivedHeader = app->createPanelHeader(MapsApp::uiIcon("archive"), "Archived Sources");
  archivedPanel = app->createMapPanel(archivedHeader, archivedContent, NULL, false);

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
  overflowMenu->addItem("Restore default sources", [=](){
    FSPath path = FSPath(app->configFile).parent().child("mapsources.default.yaml");
    importSources(path.path);
  });

  overflowMenu->addItem("Clear cache", [=](){
    static const char* wipe = "DELETE FROM images WHERE tile_id NOT IN (SELECT tile_id FROM offline_tiles); VACUUM;";
    auto& tileSources = app->map->getScene()->tileSources();
    for(auto& tilesrc : tileSources) {
      auto& info = tilesrc->offlineInfo();
      if(info.cacheFile.empty()) continue;
      MapsOffline::queueOfflineTask(0, [db=info.cacheFile](){ MapsOffline::runSQL(db, wipe); });
    }
  });
  auto clearAllCachesFn = [this](std::string res){
    if(res == "OK") {
      MapsOffline::queueOfflineTask(0, [this](){
        int64_t tot = MapsOffline::shrinkCache(20'000'000);
        app->storageTotal = tot + app->storageOffline;
      });
    }
  };
  overflowMenu->addItem("Clear all caches", [=](){
    MapsApp::messageBox("Clear all caches", "Delete all cached map data? This action cannot be undone.",
        {"OK", "Cancel"}, clearAllCachesFn);
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
  Menu* sourcesMenu = createMenu(Menu::VERT);
  //sourcesMenu->autoClose = true;
  sourcesMenu->addHandler([this, sourcesMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(sourcesMenu->selectFirst(".child-container"));
      int uiWidth = app->getPanelWidth();
      if(sourcesDirty) populateSources();
      int ii = 0;
      auto sources = sourcesContent->getOrder();
      for(const std::string& key : sources) {
        auto src = mapSources[key];
        if(!src || src["layer"].as<bool>(false)) continue;
        auto onClicked = [this, key](){
          if(sourceEditPanel->isVisible() || key == currSource)
            populateSourceEdit(key);
          else
            rebuildSource(key);
        };
        std::string title = mapSources[key]["title"].Scalar();
        Button* item = sourcesMenu->addItem(title.c_str(), MapsApp::uiIcon("layers"), onClicked);
        SvgPainter::elideText(static_cast<SvgText*>(item->selectFirst(".title")->node), uiWidth - 100);
        item->setChecked(key == currSource);
        if(++ii >= 10) break;
      }
    }
    return false;
  });

  Button* sourcesBtn = app->createPanelButton(MapsApp::uiIcon("layers"), "Sources", sourcesPanel);
  sourcesBtn->setMenu(sourcesMenu);
  return sourcesBtn;
}
