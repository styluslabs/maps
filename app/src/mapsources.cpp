#include "mapsources.h"
#include "mapsapp.h"
#include "util.h"
#include "scene/scene.h"
#include "style/style.h"  // for making uniforms avail as GUI variables
#include "data/mbtilesDataSource.h"
#include "data/networkDataSource.h"
#include "sqlitepp.h"

#include "usvg/svgpainter.h"
#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "mapwidgets.h"

#include "offlinemaps.h"
#include "plugins.h"

// Source selection

static std::string getLayerName(const YAML::Node& n) { return n.IsMap() ? n["source"].Scalar() : n.Scalar(); }

class SourceBuilder
{
public:
  const YAML::Node& sources;
  std::vector<std::string> imports;
  std::vector<SceneUpdate> updates;
  int order = 0;
  bool vectorBase = true;

  std::vector<std::string> layerkeys;

  SourceBuilder(const YAML::Node& s) : sources(s) {}

  void addLayer(const std::string& key, float opacity);
  std::string getSceneYaml(const std::string& baseUrl);
};

void SourceBuilder::addLayer(const std::string& key, float opacity)  //, const YAML::Node& src)
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
      addLayer(getLayerName(layer), layer.IsMap() ? layer["opacity"].as<float>(1.0f) : 1.0f);
    }
  }
  else if(src["url"]) {  // raster tiles
    layerkeys.push_back(key);
    std::string rasterN = fstring("raster-%d", order);
    updates.emplace_back("+sources." + rasterN + ".type", "Raster");
    for (const auto& attr : src) {
      const std::string& k = attr.first.Scalar();
      if(k != "title" && k != "archived" && k != "updates" && k != "layer" && k != "download_url")
        updates.emplace_back("+sources." + rasterN + "." + attr.first.Scalar(), yamlToStr(attr.second));
    }
    // if cache file is not explicitly specified, use key since it is guaranteed to be unique
    if(!src["cache"] || src["cache"].Scalar() != "false")
      updates.emplace_back("+sources." + rasterN + ".cache", key);
    if(MapsApp::terrain3D)
      updates.emplace_back("+sources." + rasterN + ".rasters", "global.elevation_sources");
    // separate style is required for each overlay layer
    //  use translucent instead of overlay so that depth test can place proxy tiles underneath other tiles
    //  text and points are drawn w/ blend_order -1, so use blend_order < -1 to place rasters underneath
    // note that lines and polygons are normally drawn w/ opaque blend mode, which ignores blend_order and is
    //  drawn before all other blend modes; default raster style uses opaque!
    bool isoverlay = order > 0 && src["layer"].as<bool>(false);
    if(order == 0) {
      updates.emplace_back("+styles.raster-" + key, "{ mix: raster-common }");
      vectorBase = false;
    }
    else {
      updates.emplace_back("+styles.raster-" + key, fstring("{ mix: [raster-common, raster-opacity],"
          " shaders: { uniforms: { u_opacity: %.2f } }, blend_order: %d }", opacity, order-100));
    }
    updates.emplace_back("+layers." + rasterN + ".data.source", rasterN);
    // order is ignored (and may not be required) for raster styles
    updates.emplace_back("+layers." + rasterN + ".draw.group-0.style", "raster-" + key);
    // opacity needs to be a uniform so it can be adjusted in real time w/o reloading scene
    //updates.emplace_back("+layers." + rasterN + ".draw.group-0.alpha", std::to_string(opacity));
    // 100 unit gap between rasters to allow sufficient range for proxy layers, which need ~50 unit offset
    //  to prevent proxy tile terrain from poking through; 2000 units is max order usable w/ 3D terrain
    // we should keep separate counts of base rasters and overlays to make more efficient use of order range
    int draworder = (isoverlay ? 1000 : 100) + order*100;
    updates.emplace_back("+layers." + rasterN + ".draw.group-0.order", std::to_string(draworder));
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
  const char* updkey = vectorBase ? "updates_vector" : "updates_raster";
  for(const auto& update : src[updkey])
    updates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
}

std::string SourceBuilder::getSceneYaml(const std::string& baseUrl)
{
  static const char* stylestr = R"(
styles:
  raster-opacity:
    blend: nonopaque
    shaders:
      uniforms: { u_opacity: 1.0 }
      blocks:
        color: "color.a *= u_opacity;"

  raster-common:
    base: raster
    mix: global.terrain_3d_mixin
    raster: color
    lighting: false
    shaders:
      defines: { ELEVATION_INDEX: 1 }
)";

  // we'll probably want to skip curl for reading from filesystem in scene/importer.cpp - see tests/src/mockPlatform.cpp
  // or maybe add a Url getParent() method to Url class
  std::string importstr;
  // before scene so scene can override things, but we can split into pre_imports and post_imports if needed
  for(auto& imp : MapsApp::config["sources"]["common_imports"]) {
    std::string url = imp.Scalar();
    importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
  }
  if(MapsApp::terrain3D) {
    std::string url = MapsApp::config["terrain_3d"]["import"].as<std::string>();
    if(!url.empty())
      importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
  }
  for(auto& url : imports)
    importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
  if(importstr.empty())
    return stylestr;  //"global:\n\nsources:\n\nlayers:\n";
  return "import:\n" + importstr + stylestr;
}

MapsSources::MapsSources(MapsApp* _app) : MapsComponent(_app) { reload(); }

void MapsSources::reload()
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
  sourcesDirty = true;
  sceneVarsLoaded = false;
  currSource.clear();
}

void MapsSources::addSource(const std::string& key, YAML::Node srcnode)
{
  mapSources[key] = srcnode;
  if(!mapSources[key]["__plugin"])
    saveSourcesNeeded = true;
  sourcesDirty = true;
  if(sourcesPanel && sourcesPanel->isVisible())
    populateSources();
  else if(sourceEditPanel && sourceEditPanel->isVisible() && key == currSource)
    populateSourceEdit(key);
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
  saveBtn->setEnabled(!trimStr(titleEdit->text()).empty() && (!urlEdit->isVisible() || !urlEdit->text().empty()));
}

void MapsSources::promptDownload(const std::vector<std::string>& keys)
{
  for(auto& key : keys) {
    auto src = mapSources[key];
    if(src["download_url"]) {
      int hit = 0;
      SQLiteStmt(app->bkmkDB, "SELECT COUNT(1) FROM offlinemaps WHERE source = ?;").bind(key).onerow(hit);
      if(!hit) {
        std::string dlurl = src["download_url"].Scalar();
        MapsApp::messageBox("Download tiles",
            fstring("No map tiles downloaded yet for source %s.", src["title"].Scalar().c_str()), {"Get Tiles", "Cancel"},
            [=](std::string res){ if(res != "Cancel") { MapsApp::openURL(dlurl.c_str()); } });
      }
    }
  }
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
        currLayers.push_back({src, 1.0f});  //builder.addLayer(src);
    }
    else {
      auto src = mapSources[srcname];
      if(!src) return;
      if(src["layers"] && !src["layer"].as<bool>(false)) {
        for(const auto& layer : src["layers"])
          currLayers.push_back({getLayerName(layer), layer.IsMap() ? layer["opacity"].as<float>(1.0f) : 1.0f});
        for(const auto& update : src["updates"])
          currUpdates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
      }
      else
        currLayers.push_back({srcname, 1.0f});
    }
  }

  for(auto& src : currLayers) {
    if(builder.order == 0) { src.opacity = NAN; }
    builder.addLayer(src.source, src.opacity);
  }
  if(MapsApp::terrain3D) {
    for(const auto& update : MapsApp::config["terrain_3d"]["updates"])
      builder.updates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
  }
  builder.updates.insert(builder.updates.end(), currUpdates.begin(), currUpdates.end());

  if(!builder.imports.empty() || !builder.updates.empty()) {
    // we need this to be persistent for scene reloading (e.g., on scene variable change)
    app->sceneYaml = builder.getSceneYaml(baseUrl);
    app->sceneFile = baseUrl + "__GUI_SOURCES__";
    app->sceneUpdates = std::move(builder.updates);  //.clear();
    app->loadSceneFile(async);
    sceneVarsLoaded = false;
    legendsLoaded = false;
    if(!srcname.empty()) {
      currSource = srcname;
      app->config["sources"]["last_source"] = currSource;
    }
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
        bool shown = std::find_if(currLayers.begin(), currLayers.end(),
            [&](auto& l) { return l.source == key; }) != currLayers.end()
            || std::find(builder.layerkeys.begin(), builder.layerkeys.end(), key) != builder.layerkeys.end();
        showbtn->setChecked(key != currSource && shown);
      }
    }
    if(async)  // don't prompt for download when importing tiles!
      promptDownload(builder.layerkeys);
  }

  saveBtn->setEnabled(srcname.empty());  // for existing source, don't enable saveBtn until edited
}

std::string MapsSources::createSource(std::string savekey, const std::string& yamlStr)
{
  if(savekey.empty()) {
    // find available name
    int ii = mapSources.size();
    while(ii < INT_MAX && mapSources[fstring("custom-%d", ii)]) ++ii;
    savekey = fstring("custom-%d", ii);
  }

  if(!yamlStr.empty()) {
    try {
      mapSources[savekey] = YAML::Load(yamlStr);
    } catch (...) {
      return "";
    }
  }
  else {
    YAML::Node node = mapSources[savekey] ? mapSources[savekey] : YAML::Node(YAML::NodeType::Map);
    node["title"] = trimStr(titleEdit->text());
    if(node["layers"] || !mapSources[savekey]) {
      YAML::Node layers = YAML::Node(YAML::NodeType::Sequence);
      for(auto& src : currLayers)
        layers.push_back(YAML::Load(src.opacity < 1 ?
            fstring("{source: %s, opacity: %.2f}", src.source.c_str(), src.opacity) : src.source));
      node["layers"] = layers;
    }
    else if(node["url"])
      node["url"] = trimStr(urlEdit->text());
    YAML::Node updates = node["updates"] = YAML::Node(YAML::NodeType::Map);
    // note that gui var changes will come after any defaults in currUpdates and thus replace them as desired
    for(const SceneUpdate& upd : currUpdates)   //app->sceneUpdates) {
      updates[upd.path[0] == '+' ? upd.path.substr(1) : upd.path] = upd.value;
    if(!mapSources[savekey]) mapSources[savekey] = node;
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
  sourcesContent->onApplyLayout = {};
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
    bool isRaster = bool(src["url"]);
    if(!src["layers"] || isLayer) {
      layerKeys.push_back(key);
      layerTitles.push_back(src["title"].Scalar());
    }

    Button* item = createListItem(MapsApp::uiIcon("layers"),
        src["title"].Scalar().c_str(), src["description"].Scalar().c_str());
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

    Button* editBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit");
    if(isLayer || isRaster) {
      Button* showBtn = createToolbutton(MapsApp::uiIcon("eye"), "Show");
      showBtn->node->addClass("show-btn");
      showBtn->onClicked = [=](){
        if(key == currSource) return;
        bool show = !showBtn->isChecked();
        showBtn->setChecked(show);
        if(!show)
          currLayers.erase(std::remove(currLayers.begin(), currLayers.end(), key), currLayers.end());
        else if(!isLayer) {
          // insert before first layer that is not an opaque raster
          auto it = currLayers.begin();
          for(; it != currLayers.end(); ++it) {
            auto itsrc = mapSources[it->source];
            if(!itsrc["url"] || itsrc["layer"].as<bool>(false)) break;
          }
          currLayers.insert(it, {key, 1.0f});
        }
        else
          currLayers.push_back({key, 1.0f});
        // treat as new source (to edit an existing multi-layer source, user can go to edit sources directly)
        currSource = "";
        titleEdit->setText("Untitled");
        rebuildSource();  //currLayers
      };
      container->addWidget(showBtn);
      item->onClicked = [=](){
        showBtn->setChecked(false);
        if(key != currSource)
          rebuildSource(key);
      };
      editBtn->onClicked = [=](){ populateSourceEdit(showBtn->isChecked() ? currSource : key); };
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
      if(key == currSource)
        currSource.clear();
      app->gui->deleteWidget(item);  //populateSources();
    };
    overflowMenu->addItem("Delete", [=](){
      std::vector<std::string> dependents;
      for (const auto& ssrc : mapSources) {
        for (const auto& layer : ssrc.second["layers"]) {
          if(getLayerName(layer) == key)
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
      currLayers.push_back({layerKeys[idx], 1.0f});
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
    ordercfg.SetStyle(YAML::EmitterStyle::Flow);
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
    app->notifyStatusBarBG(!app->readSceneValue("application.dark_base_map").as<bool>(false));
    // load legend widgets
    app->gui->deleteContents(legendMenu->selectFirst(".child-container"));
    app->gui->deleteContents(app->legendContainer);
    YAML::Node legends = app->readSceneValue("application.legend");
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

static Tangram::Style* findStyle(Tangram::Scene* scene, const std::string name)
{
  auto& styles = scene->styles();
  for(auto& style : styles) {
    if(style->getName() == name)
      return style.get();
  }
  return NULL;
}

Widget* MapsSources::processUniformVar(const std::string& stylename, const std::string& varname, YAML::Node varnode)
{
  Tangram::Style* style = findStyle(app->map->getScene(), stylename);
  if(style) {
    for(auto& uniform : style->styleUniforms()) {
      if(uniform.first.name == varname) {
        if(uniform.second.is<float>()) {
          float stepval = varnode["step"].as<float>(1);
          float minval = varnode["min"].as<float>(-INFINITY);
          float maxval = varnode["max"].as<float>(INFINITY);
          auto spinBox = createTextSpinBox(uniform.second.get<float>(), stepval, minval, maxval, "%.2f");
          spinBox->onValueChanged = [=, &uniform](real val){
            std::string path = "styles." + stylename + ".shaders.uniforms." + varname;
            std::string newval = std::to_string(val);
            replaceSceneVar(app->sceneUpdates, path, newval);
            replaceSceneVar(currUpdates, path, newval);
            uniform.second.set<float>(val);
            app->platform->requestRender();
            sourceModified();
          };
          return spinBox;
        }
        LOGE("Cannot set %s.%s: only float uniforms currently supported in gui_variables!", stylename.c_str(), varname.c_str());
        return NULL;
      }
    }
  }
  LOGE("Cannot find style uniform %s.%s referenced in gui_variables!", stylename.c_str(), varname.c_str());
  return NULL;
}

void MapsSources::populateSceneVars()
{
  sceneVarsLoaded = true;
  app->gui->deleteContents(varsContent);

  YAML::Node vars = app->readSceneValue("application.gui_variables");
  for(const auto& var : vars) {
    std::string name = var.first.Scalar();  //.as<std::string>("");
    std::string label = var.second["label"].as<std::string>("");
    std::string onchange = var.second["onchange"].as<std::string>("");
    std::string stylename = var.second["style"].as<std::string>("");
    bool reload = var.second["reload"].as<std::string>("") != "false";
    if(!varsContent->containerNode()->children().empty())
      varsContent->addWidget(createHRule(1));
    if(!stylename.empty()) {  // shader uniform
      Widget* uwidget = processUniformVar(stylename, name, var.second);
      if(uwidget)
        varsContent->addWidget(createTitledRow(label.c_str(), uwidget));
    }
    else {  // global variable
      std::string value = yamlToStr(app->readSceneValue("global." + name));  //.as<std::string>("");
      std::string valtype = var.second["type"].as<std::string>("");
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
      else if(valtype == "date") {
        auto parts = splitStr<std::vector>(value, " -/");
        if(parts.size() != 3) parts = {"2024", "01", "01"};
        int year0 = atoi(parts[0].c_str());
        int month0 = atoi(parts[1].c_str());  // + (parts[1].front() == '0' ? 1 : 0));
        int day0 = atoi(parts[2].c_str());  // + (parts[2].front() == '0' ? 1 : 0));
        auto datepicker = createDatePicker(year0, month0, day0, [=](int year, int month, int day){
          updateSceneVar("global." + name, fstring("%04d-%02d-%02d", year, month, day), onchange, reload);
        });
        varsContent->addWidget(createTitledRow(label.c_str(), NULL, datepicker));
      }
      else if(var.second["min"] || var.second["max"] || var.second["step"] || valtype == "int") {
        float stepval = var.second["step"].as<float>(1);
        float minval = var.second["min"].as<float>(-INFINITY);
        float maxval = var.second["max"].as<float>(INFINITY);
        const char* format = valtype == "int" ? "%.0f" : "%.2f";
        auto spinBox = createTextSpinBox(atof(value.c_str()), stepval, minval, maxval, format);
        spinBox->onValueChanged = [=](real val){
          updateSceneVar("global." + name, std::to_string(val), onchange, reload);
        };
        varsContent->addWidget(createTitledRow(label.c_str(), spinBox));
      }
      else {
        auto textedit = createTitledTextEdit(label.c_str(), value.c_str());
        textedit->addHandler([=](SvgGui* gui, SDL_Event* event){
          if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_RETURN) {
            updateSceneVar("global." + name, trimStr(textedit->text()), onchange, reload);
            return true;
          }
          return false;
        });
        varsContent->addWidget(textedit);
      }
    }
  }
  varsSeparator->setVisible(!varsContent->containerNode()->children().empty());

  std::string credits;
  YAML::Node srcs = app->readSceneValue("sources");
  for(const auto& src : srcs) {
    //std::string name = var.first.Scalar();  //.as<std::string>("");
    std::string credit = src.second["attribution"].as<std::string>("");
    if(!credit.empty()) credits.append(credit).append("\n");
  }
  creditsText->setText(credits.c_str());
  if(!credits.empty())
    creditsText->setText(SvgPainter::breakText(
        static_cast<SvgText*>(creditsText->node), app->getPanelWidth() - 30).c_str());
}

void MapsSources::populateSourceEdit(std::string key)
{
  app->showPanel(sourceEditPanel, true);
  app->gui->deleteContents(layersContent);
  if(currSource != key)
    rebuildSource(key);
  auto src = mapSources[key];
  saveBtn->setEnabled(!src);
  if(src)
    titleEdit->setText(mapSources[key]["title"].Scalar().c_str());

  if(!src || src["layers"]) {
    for(auto& layer : currLayers) {
      std::string layername = layer.source;
      Button* item = createListItem(MapsApp::uiIcon("layers"), mapSources[layername]["title"].Scalar().c_str());
      Widget* container = item->selectFirst(".child-container");

      // if raster layer, show opacity control
      if(mapSources[layername]["url"] && !std::isnan(layer.opacity)) {
        Button* opacityBtn = createToolbutton(
            MapsApp::uiIcon("opacity"), fstring("%d%%", int(layer.opacity*100 + 0.5f)).c_str(), true);

        Slider* opacitySlider = createSlider();
        opacitySlider->setValue(layer.opacity);
        opacitySlider->onValueChanged = [=](real val){
          // can't cache style because Scene could be reloaded w/o repeating populateSourceEdit()
          Tangram::Style* style = findStyle(app->map->getScene(), "raster-" + layername);
          if(!style) return;
          opacityBtn->setTitle(fstring("%d%%", int(val*100 + 0.5)).c_str());
          auto it = std::find(currLayers.begin(), currLayers.end(), layername);
          if(it != currLayers.end())
            it->opacity = val;
          for(auto& uniform : style->styleUniforms()) {
            if(uniform.first.name == "u_opacity" && uniform.second.is<float>()) {
              uniform.second.set<float>(val);
              app->platform->requestRender();
              sourceModified();
              break;
            }
          }
        };
        setMinWidth(opacitySlider, 250);

        Widget* opacityWidget = new Widget(new SvgG());
        opacityWidget->node->setAttribute("layout", "box");
        opacityWidget->node->setAttribute("margin", "0 6");
        opacityWidget->addWidget(opacitySlider);

        // if we use VERT_RIGHT, slider shifts when toolbutton text width changes (and slider gets shifted
        //  over anyway in narrow layout mode)
        Menu* opacityMenu = createMenu(Menu::VERT_LEFT);
        opacityMenu->addWidget(opacityWidget);
        opacityMenu->isPressedGroupContainer = false;  // otherwise slider handle can't be dragged
        //widthPreview->onPressed = [this](){ window()->gui()->pressedWidget = NULL; };
        opacityBtn->setMenu(opacityMenu);
        container->addWidget(opacityBtn);
      }

      Button* discardBtn = createToolbutton(MapsApp::uiIcon("discard"), "Remove");
      discardBtn->onClicked = [=](){
        currLayers.erase(std::remove(currLayers.begin(), currLayers.end(), layername), currLayers.end());
        rebuildSource();
        sourceModified();
        app->gui->deleteWidget(item);
      };
      container->addWidget(discardBtn);
      layersContent->addWidget(item);
    }

    Button* item = createListItem(MapsApp::uiIcon("add"), "Add Layer...");
    item->onClicked = [=](){ showModalCentered(selectLayerDialog.get(), MapsApp::gui); };
    layersContent->addWidget(item);
    urlEdit->setVisible(false);
  }
  else if(src["url"]) {  // raster tiles
    urlEdit->setVisible(true);
    urlEdit->setText(src["url"].Scalar().c_str());
  }
  else  // vector scene
    urlEdit->setVisible(true);

  if(app->map->getScene()->isReady())
    populateSceneVars();
}

void MapsSources::importSources(const std::string& src)
{
  std::string key;
  if(src.empty()) {}
  else if(src.front() == '{') {
    key = createSource("", src);
  }
  else if(Tangram::NetworkDataSource::urlHasTilePattern(src)) {
    key = createSource("", fstring(R"({title: "New Source", url: "%s"})", src.c_str()));
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
  // Source list panel
  Toolbar* importTb = createToolbar();
  TextEdit* importEdit = createTitledTextEdit("URL or YAML");
  Button* importFileBtn = createToolbutton(MapsApp::uiIcon("open-folder"), "Open file...");
  importTb->addWidget(importFileBtn);
  importTb->addWidget(importEdit);

  auto importFileFn = [=](std::unique_ptr<PlatformFile> file){
    importDialog->finish(Dialog::CANCELLED);
    importSources(file->readAll().data());
  };
  importFileBtn->onClicked = [=](){ MapsApp::openFileDialog({{"YAML files", "yaml,yml"}}, importFileFn); };

  importDialog.reset(createInputDialog({importTb}, "Import source", "Import", [=](){
    // JSON (YAML flow), tile URL, or path/URL to file
    importSources(trimStr(importEdit->text()));
  }));

  Button* createBtn = createToolbutton(MapsApp::uiIcon("add"), "New Source");
  createBtn->onClicked = [=](){
    currSource = "";
    populateSourceEdit("");  // so user can edit title
    titleEdit->setText("Untitled");
  };

  archivedContent = createColumn();
  auto archivedHeader = app->createPanelHeader(MapsApp::uiIcon("archive"), "Archived Sources");
  archivedPanel = app->createMapPanel(archivedHeader, archivedContent, NULL, false);

  sourcesContent = new DragDropList;

  Widget* offlineBtn = app->mapsOffline->createPanel();

  legendBtn = createToolbutton(MapsApp::uiIcon("map-question"), "Legends");
  legendMenu = createMenu(Menu::VERT_LEFT);
  legendBtn->setMenu(legendMenu);
  legendBtn->setVisible(false);

  Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
  Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
  overflowBtn->setMenu(overflowMenu);
  overflowMenu->addItem("Import source", [=](){
    importDialog->focusedWidget = NULL;
    showModalCentered(importDialog.get(), app->gui);  //showInlineDialogModal(editContent);
    importEdit->setText("");
    app->gui->setFocused(importEdit, SvgGui::REASON_TAB);
  });
  overflowMenu->addItem("Restore default sources", [=](){
    FSPath path = FSPath(app->configFile).parent().child("mapsources.default.yaml");
    importSources("file://" + path.path);
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
        int64_t tot = MapsOffline::shrinkCache(0);  //20'000'000);
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
  sourcesPanel = app->createMapPanel(sourcesHeader, NULL, sourcesContent, false);

  sourcesPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      if(sourcesDirty)
        populateSources();
    }
    return false;
  });

  // Source edit panel
  titleEdit = createTitledTextEdit("Title");
  titleEdit->node->setAttribute("box-anchor", "hfill");
  urlEdit = createTitledTextEdit("URL");
  urlEdit->node->setAttribute("box-anchor", "hfill");
  urlEdit->node->setAttribute("margin", "0 3");
  saveBtn = createToolbutton(MapsApp::uiIcon("save"), "Save Source");
  //discardBtn = createToolbutton(MapsApp::uiIcon("discard"), "Delete Source");
  saveBtn->node->setAttribute("box-anchor", "bottom");
  saveBtn->onClicked = [=](){
    createSource(currSource);
    saveBtn->setEnabled(false);
  };
  // we should check for conflict w/ title of other source here
  titleEdit->onChanged = [this](const char*){ sourceModified(); };
  urlEdit->onChanged = [this](const char*){ sourceModified(); };

  Widget* sourceTb = createRow({titleEdit, saveBtn});  //createToolbar
  sourceTb->node->setAttribute("margin", "0 3");
  varsContent = createColumn();
  varsContent->node->setAttribute("box-anchor", "hfill");
  varsContent->node->setAttribute("margin", "0 10");
  layersContent = createColumn();
  layersContent->node->setAttribute("box-anchor", "hfill");
  varsSeparator = createHRule(2, "2 6 0 6");
  varsSeparator->setVisible(false);
  creditsText = new TextBox(createTextNode(""));
  creditsText->node->addClass("weak");
  creditsText->node->setAttribute("font-size", "12");
  creditsText->node->setAttribute("margin", "8 0");
  Widget* srcEditContent = createColumn(
      {sourceTb, urlEdit, createHRule(2, "8 6 0 6"), varsContent, varsSeparator, layersContent, creditsText});

  auto editHeader = app->createPanelHeader(MapsApp::uiIcon("edit"), "Edit Source");
  sourceEditPanel = app->createMapPanel(editHeader, srcEditContent);  //, sourceTb);

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
