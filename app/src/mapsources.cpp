#include "mapsources.h"
#include "mapsapp.h"
#include "util.h"
#include "scene/scene.h"
#include "style/style.h"  // for making uniforms avail as GUI variables
#include "data/mbtilesDataSource.h"
#include "data/networkDataSource.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "mapwidgets.h"

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

  for(const auto& update : src["updates"]) {
    updates.emplace_back("+" + update.first.Scalar(), yamlToStr(update.second));
  }
}

std::string SourceBuilder::getSceneYaml(const std::string& baseUrl)
{
  // we'll probably want to skip curl for reading from filesystem in scene/importer.cpp - see tests/src/mockPlatform.cpp
  // or maybe add a Url getParent() method to Url class
  std::string importstr;
  for(auto& url : imports)
    importstr += "  - " + (url.find("://") == std::string::npos ? baseUrl : "") + url + "\n";
  for(auto& imp : MapsApp::config["common_imports"]) {
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

  FSPath srcfile = path.child(app->config["sources"].Scalar());
  try {
    mapSources = YAML::LoadFile(srcfile.c_str());
  } catch (std::exception& e) {}
  if(!mapSources) {
    try {
      mapSources = YAML::LoadFile(srcfile.parent().childPath(srcfile.baseName() + ".default.yaml"));
    } catch (std::exception& e) {}
  }

  /*std::string srcyaml;
  for(auto srcfile : app->config["sources"]) {
    FSPath srcpath = path.child(srcfile.Scalar());
    // how to handle baseUrl if sources files are in different folders?
    //if(baseUrl.empty())
    //  baseUrl = "file://" + srcpath.parentPath();
    if(!readFile(&srcyaml, srcpath.c_str()))
      LOGW("Unable to open map sources file %s", srcpath.c_str());
    srcyaml += "\n";  // to handle source files w/o trailing newline
  }
  mapSources = YAML::Load(srcyaml.c_str());*/

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
  */
}

// this should be static fns!
void MapsSources::deleteOfflineMap(int mapid)
{
  int64_t offlineSize = 0;
  FSPath cachedir(app->baseDir, "cache");
  for(auto& file : lsDirectory(cachedir)) {
    //for(auto& src : mapSources) ... this doesn't work because cache file may be specified in scene yaml
    //std::string cachename = src.second["cache"] ? src.second["cache"].Scalar() : src.first.Scalar();
    //std::string cachefile = app->baseDir + "cache/" + cachename + ".mbtiles";
    //if(cachename == "false" || !FSPath(cachefile).exists()) continue;
    FSPath cachefile = cachedir.child(file);
    if(cachefile.extension() != "mbtiles") continue;
    auto s = std::make_unique<Tangram::MBTilesDataSource>(
        *app->platform, cachefile.baseName(), cachefile.path, "", true);
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

  FSPath cachedir(app->baseDir, "cache");
  for(auto& file : lsDirectory(cachedir)) {
    FSPath cachefile = cachedir.child(file);
    if(cachefile.extension() != "mbtiles") continue;
    dbsources.push_back(std::make_unique<Tangram::MBTilesDataSource>(
        *app->platform, cachefile.baseName(), cachefile.path, "", true));
    dbsources.back()->getTileSizes(insertTile);
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
  // TODO: make a copy of mapSources w/ sources from plugins removed
  YAML::Emitter emitter;
  //emitter.SetStringFormat(YAML::DoubleQuoted);
  emitter << mapSources;
  FileStream fs(app->config["sources"].Scalar().c_str(), "wb");
  fs.write(emitter.c_str(), emitter.size());
}

void MapsSources::sourceModified()
{
  saveBtn->setEnabled(true);
}

// New GUI

constexpr int MAX_SOURCES = 8;

void MapsSources::rebuildSource(const std::string& srcname)
{
  SourceBuilder builder(mapSources);
  // support comma separated list of sources
  auto splitsrc = splitStr<std::vector>(srcname, ",");
  if(splitsrc.size() > 1) {
    for(auto& src : splitsrc)
      builder.addLayer(src);
  }
  else if(!srcname.empty()) {
    builder.addLayer(srcname);
  }
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
    sceneVarsLoaded = false;
    currSource = srcname.empty() ? joinStr(builder.layerkeys, ",") : srcname;
    app->config["last_source"] = currSource;
  }

  bool multi = !mapSources[srcname]["layers"].IsNull(); //builder.layerkeys.size() > 1;
  saveBtn->setEnabled(srcname.empty());  // for existing source, don't enable saveBtn until edited

  size_t ii = 0;
  nSources = int(builder.layerkeys.size());  //std::max(int(builder.layerkeys.size()), nSources);
  for(; ii < builder.layerkeys.size(); ++ii) {
    for(size_t jj = 0; jj < layerKeys.size(); ++jj) {
      if(builder.layerkeys[ii] == layerKeys[jj]) {
        //currSrcIdx[ii] = jj;
        layerCombos[ii]->setIndex(jj);
        layerRows[ii]->setVisible(true);
        break;  // next layer
      }
    }
  }

  layerCombos[ii]->setIndex(0);
  layerRows[ii]->setVisible(multi);

  for(++ii; ii < layerRows.size(); ++ii)
    layerRows[ii]->setVisible(false);
}

void MapsSources::createSource(std::string savekey, const std::string& newSrcTitle)
{
  if(savekey.empty()) {
    // find available name
    int ii = mapSources.size();
    while(ii < INT_MAX && mapSources[fstring("custom-%d", ii)]) ++ii;
    savekey = fstring("custom-%d", ii);

    mapSources[savekey] = YAML::Node(YAML::NodeType::Map);
    mapSources[savekey]["type"] = "Multi";
  }
  YAML::Node node = mapSources[savekey];
  node["title"] = newSrcTitle;

  if(node["type"].Scalar() == "Multi") {
    YAML::Node layers = node["layers"] = YAML::Node(YAML::NodeType::Sequence);
    for(int ii = 0; ii < nSources; ++ii) {
      int idx = layerCombos[ii]->index();
      if(idx > 0)
        layers.push_back(YAML::Load("{source: " + layerKeys[idx] + "}"));
    }
  }

  YAML::Node updates = node["updates"] = YAML::Node(YAML::NodeType::Map);
  for(const SceneUpdate& upd : app->sceneUpdates)
    updates[upd.path] = upd.value;

  saveSources();
  populateSources();

  /*
  std::stringstream fs;  //fs(sourcesFile, std::fstream::app | std::fstream::binary);

  //if(currIdx > 0 && newSrcTitle == titles[currIdx] && mapSources[keys[currIdx]]["type"].Scalar() == "Multi")

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
  */
}

void MapsSources::createSource(std::string savekey, const YAML::Node& node)
{
  mapSources[savekey] = node;
  saveSources();
  populateSources();
}


void MapsSources::populateSources()
{
  app->gui->deleteContents(sourcesContent, ".listitem");

  std::vector<std::string> layerTitles = {"None"};
  std::vector<std::string> sourceTitles = {};
  layerKeys = {""};
  sourceKeys = {};
  for(const auto& src : mapSources) {
    std::string key = src.first.Scalar();
    if(!src.second["layer"].as<bool>(false) && src.second["type"].Scalar() != "Update") {
      sourceKeys.push_back(key);
      sourceTitles.push_back(src.second["title"].Scalar());
    }
    if(src.second["type"].Scalar() != "Multi") {
      layerKeys.push_back(key);
      layerTitles.push_back(src.second["title"].Scalar());
    }

    Button* item = new Button(sourceListProto->clone());
    Widget* container = item->selectFirst(".child-container");
    item->onClicked = [this, key](){ rebuildSource(key); };

    Button* editBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_draw.svg"), "Show");
    editBtn->onClicked = [this, key](){ populateSourceEdit(key); };

    Button* overflowBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_overflow.svg"), "More");
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowMenu->addItem("Delete", [=](){
      mapSources.remove(key);
      populateSources();
    });

    container->addWidget(editBtn);
    container->addWidget(overflowBtn);
    item->selectFirst(".title-text")->setText(src.second["title"].Scalar().c_str());
    //item->selectFirst(".detail-text")->setText(track->detail.c_str());
    sourcesContent->addWidget(item);
  }
  //sourceCombo->addItems(sourceTitles);
  for(SelectBox* combo : layerCombos)
    combo->addItems(layerTitles);
}

void MapsSources::onMapChange()
{
  if(!sceneVarsLoaded && app->map->getScene()->isReady() && sourceEditPanel->isVisible())
    populateSceneVars();
}

void MapsSources::populateSceneVars()
{
  sceneVarsLoaded = true;
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
                  sourceModified();
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
        sourceModified();
        if(reload == "false")  // ... so default to reloading
          app->map->updateGlobals({app->sceneUpdates.back()});  //SceneUpdate{"global." + name, newval ? "true" : "false"}});
        else
          app->loadSceneFile();  //{SceneUpdate{"global." + name, newval ? "true" : "false"}});
      };
      varsContent->addWidget(createTitledRow(label.c_str(), checkbox));
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

  if(app->map->getScene()->isReady())
    populateSceneVars();
}

Widget* MapsSources::createPanel()
{
  static const char* sourceListProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g class="child-container" layout="flex" flex-direction="row" box-anchor="hfill">
        <g class="image-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_drawer.svg"/>
        </g>
        <g layout="box" box-anchor="vfill">
          <text class="title-text" box-anchor="left" margin="0 10"></text>
          <text class="detail-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
        </g>

        <rect class="stretch" fill="none" box-anchor="fill" width="20" height="20"/>

      </g>
    </g>
  )";
  sourceListProto.reset(loadSVGFragment(sourceListProtoSVG));

  Toolbar* sourceTb = createToolbar();
  titleEdit = createTextEdit();
  saveBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_save.svg"), "Save Source");
  //discardBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_discard.svg"), "Delete Source");
  sourceTb->addWidget(titleEdit);
  sourceTb->addWidget(saveBtn);

  Toolbar* importTb = createToolbar();
  TextEdit* importEdit = createTextEdit();
  Button* importAccept = createToolbutton(SvgGui::useFile(":/icons/ic_menu_accept.svg"), "Save");
  Button* importCancel = createToolbutton(SvgGui::useFile(":/icons/ic_menu_cancel.svg"), "Cancel");
  importTb->addWidget(importEdit);
  importTb->addWidget(importAccept);
  importTb->addWidget(importCancel);

  Button* importBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_expanddown.svg"), "Import");
  importBtn->onClicked = [=](){ importEdit->setText("");  importTb->setVisible(true); };
  importCancel->onClicked = [=](){ importTb->setVisible(false); };

  // JSON (YAML flow), tile URL, or path/URL to file
  importAccept->onClicked = [=](){


    std::string src = importEdit->text();
    importTb->setVisible(false);

    if(src.back() == '}') {
      createSource("", YAML::Load(src));
    }
    else if(Tangram::NetworkDataSource::urlHasTilePattern(src)) {
      createSource("", YAML::Load(fstring("{type: Raster, title: 'New Source', url: %s}", src.c_str())));
    }
    else {

      // source name conflicts
      // - skip, replace, rename, or cancel?
      // - dialog on first conflict? choose before import?

      auto cb = [this, src](UrlResponse&& response) {
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
      };

      app->platform->startUrlRequest(Url(src), [cb](UrlResponse&& res){ MapsApp::runOnMainThread(cb(std::move(res))); });
      return;

    }

    populateSourceEdit();  // so user can edit title
  };


  // how to create source?
  // - currSource = ""?
  // - what is initial layer? none? (show blank map?) current source when createBtn clicked?

  Button* createBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_plus.svg"), "New Source");
  createBtn->onClicked = [=](){
    populateSourceEdit();  // so user can edit title
  };

  saveBtn->onClicked = [=](){
    createSource("", titleEdit->text());
    saveBtn->setEnabled(false);
  };

  // we should check for conflicting w/ title of other source here
  titleEdit->onChanged = [this](const char*){ saveBtn->setEnabled(true); };

  //saveBtn->onClicked = [this](){
  //  std::string key = sourceKeys[sourceCombo->index()];
  //  createSource(key, mapSources[key]["title"].Scalar());
  //};
  //
  //discardBtn->onClicked = [this](){
  //  mapSources.remove(sourceKeys[sourceCombo->index()]);
  //  populateSources();
  //};

  sourcesContent = createColumn();

  Widget* layersContent = createColumn();
  varsContent = createColumn();
  layersContent->addWidget(varsContent);
  for(int ii = 1; ii <= MAX_SOURCES; ++ii) {
    layerCombos.push_back(createSelectBox(fstring("Layer %d", ii).c_str(), SvgGui::useFile(":/icons/ic_menu_cloud.svg") , {}));
    layerCombos.back()->onChanged = [this](int){
      rebuildSource();
    };
    layerRows.push_back(createTitledRow(fstring("Layer %d", ii).c_str(), layerCombos.back()));
    layersContent->addWidget(layerRows.back());
  }
  populateSources();

  auto clearCacheFn = [this](std::string res){
    if(res == "OK") {
      shrinkCache(20'000'000);  // 20MB just to test shrinkCache code
      app->storageTotal = app->storageOffline;
    }
  };
  Button* clearCacheBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_erase.svg"), "Clear cache");
  clearCacheBtn->onClicked = [=](){
    MapsApp::messageBox("Clear cache", "Delete all cached map data? This action cannot be undone.",
        {"OK", "Cancel"}, clearCacheFn);
  };

  Widget* offlineBtn = app->mapsOffline->createPanel();

  auto sourcesHeader = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_cloud.svg"), "Map Source");
  sourcesHeader->addWidget(createStretch());
  sourcesHeader->addWidget(createBtn);
  sourcesHeader->addWidget(importBtn);
  sourcesHeader->addWidget(offlineBtn);
  sourcesHeader->addWidget(clearCacheBtn);
  sourcesPanel = app->createMapPanel(sourcesHeader, sourcesContent);

  auto editHeader = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_cloud.svg"), "Edit Source");
  sourceEditPanel = app->createMapPanel(editHeader, layersContent, newSrcTb);

  // main toolbar button
  Menu* sourcesMenu = createMenu(Menu::VERT_LEFT);
  sourcesMenu->autoClose = true;
  sourcesMenu->addHandler([this, sourcesMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(sourcesMenu->selectFirst(".child-container"));
      for(int ii = 0; ii < 10 && ii < sourceKeys.size(); ++ii) {
        std::string key = sourceKeys[ii];
        sourcesMenu->addItem(mapSources[key]["title"].Scalar().c_str(),
            [this, key](){ rebuildSource(key); });
      }
    }
    return false;
  });

  Button* sourcesBtn = app->createPanelButton(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Sources");
  sourcesBtn->setMenu(sourcesMenu);
  sourcesBtn->onClicked = [this](){
    app->showPanel(sourcesPanel);
  };

  return sourcesBtn;
}
