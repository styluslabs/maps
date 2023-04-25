#include "mapsources.h"
#include "mapsapp.h"
#include "util.h"
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


class SelectBox : public Widget
{
public:
  SelectBox(SvgNode* boxnode, SvgDocument* dialognode, const std::vector<std::string>& _items = {});
  void setText(const char* s) { comboText->setText(s); }
  int index() const { return currIndex; }
  void setIndex(int idx);
  void updateIndex(int idx);
  void addItems(const std::vector<std::string>& _items, bool replace = true);

  std::function<void(int)> onChanged;

private:
  Dialog* dialog;
  Widget* content;
  TextBox* comboText;
  int currIndex;

  std::vector<std::string> items;
};

SelectBox::SelectBox(SvgNode* boxnode, SvgDocument* dialognode, const std::vector<std::string>& _items)
    : Widget(boxnode), currIndex(0)
{
  comboText = new TextBox(containerNode()->selectFirst(".combo_text"));
  Button* comboopen = new Button(containerNode()->selectFirst(".combo_content"));

  dialog = new Dialog( setupWindowNode(dialognode) );
  content = createColumn();

  Button* cancelBtn = new Button(dialog->containerNode()->selectFirst(".cancel-btn"));
  cancelBtn->onClicked = [=](){ dialog->finish(Dialog::CANCELLED); };

  Widget* dialogBody = dialog->selectFirst(".body-container");
  dialogBody->addWidget(new ScrollWidget(new SvgDocument(), content));

  comboopen->onClicked = [this](){
    SvgGui* gui = window()->gui();
    gui->showModal(dialog, gui->windows.front()->modalOrSelf());
  };

  addItems(_items);
  setText(items.front().c_str());
}

void SelectBox::addItems(const std::vector<std::string>& _items, bool replace)
{
  if(replace) {
    window()->gui()->deleteContents(content);
    items.clear();
  }
  SvgNode* proto = dialog->containerNode()->selectFirst(".listitem-proto");
  for(int ii = 0; ii < int(_items.size()); ++ii) {
    Button* btn = new Button(proto->clone());
    btn->setVisible(true);
    btn->onClicked = [=](){
      dialog->finish(Dialog::ACCEPTED);
      updateIndex(ii);
    };
    SvgText* titlenode = static_cast<SvgText*>(btn->containerNode()->selectFirst(".title-text"));
    titlenode->addText(_items[ii].c_str());
    content->addWidget(btn);
    items.emplace_back(_items[ii]);
  }
}

void SelectBox::updateIndex(int idx)
{
  setIndex(idx);
  if(onChanged)
    onChanged(idx);
}

void SelectBox::setIndex(int idx)
{
  if(idx >= 0 && idx < int(items.size())) {
    const char* s = items[idx].c_str();
    setText(s);
    currIndex = idx;
  }
}

SelectBox* createSelectBox(const char* title, const SvgNode* itemicon, const std::vector<std::string>& items)
{
  static const char* boxProtoSVG = R"#(
    <g class="inputbox combobox" layout="box" box-anchor="left" margin="0 10">
      <rect class="min-width-rect" width="150" height="36" fill="none"/>
      <rect class="inputbox-bg" box-anchor="fill" width="150" height="36"/>

      <g class="combo_content" box-anchor="fill" layout="flex" flex-direction="row" margin="0 2">
        <g class="textbox combo_text" box-anchor="fill" layout="box">
          <text box-anchor="left" margin="3 6"></text>
        </g>
        <g class="combo_open" box-anchor="vfill" layout="box">
          <rect fill="none" box-anchor="vfill" width="28" height="28"/>
          <use class="icon" width="28" height="28" xlink:href=":/icons/chevron_down.svg" />
        </g>
      </g>
    </g>
  )#";
  static std::unique_ptr<SvgNode> boxProto;
  if(!boxProto)
    boxProto.reset(loadSVGFragment(boxProtoSVG));

  static const char* dialogProtoSVG = R"#(
    <svg id="dialog" class="window dialog" layout="box">
      <rect class="dialog-bg background" box-anchor="fill" width="20" height="20"/>
      <g class="dialog-layout" box-anchor="fill" layout="flex" flex-direction="column">
        <g class="titlebar-container" box-anchor="hfill" layout="box">
          <g class="title-container" box-anchor="hfill" layout="flex" flex-direction="row" justify-content="center">
            <text class="dialog-title"></text>
          </g>
          <g class="button-container toolbar" box-anchor="hfill" layout="flex" flex-direction="row">
            <g class="toolbutton cancel-btn" layout="box">
              <rect class="background" box-anchor="hfill" width="36" height="42"/>
              <g margin="0 3" box-anchor="fill" layout="flex" flex-direction="row">
                <use class="icon" height="36" xlink:href=":/icons/ic_menu_back.svg" />
                <text class="title" display="none" margin="0 9">Cancel</text>
              </g>
            </g>
          </g>
        </g>
        <rect class="hrule title" box-anchor="hfill" width="20" height="2"/>
        <g class="body-container" box-anchor="fill" layout="box"></g>
      </g>

      <g class="listitem-proto listitem" display="none" margin="0 5" layout="box" box-anchor="hfill">
        <rect box-anchor="fill" width="48" height="48"/>
        <g layout="flex" flex-direction="row" box-anchor="left">
          <g class="image-container" margin="2 5">
            <use class="listitem-icon" width="36" height="36" xlink:href=""/>
          </g>
          <g layout="box" box-anchor="vfill">
            <text class="title-text" box-anchor="left" margin="0 10"></text>
          </g>
        </g>
      </g>

    </svg>
  )#";
  static std::unique_ptr<SvgDocument> dialogProto;
  if(!dialogProto)
    dialogProto.reset(static_cast<SvgDocument*>(loadSVGFragment(dialogProtoSVG)));

  SvgDocument* dialog = dialogProto->clone();
  static_cast<SvgText*>(dialog->selectFirst(".dialog-title"))->addText("title");
  static_cast<SvgUse*>(dialog->selectFirst(".listitem-icon"))->setTarget(itemicon);
  SelectBox* widget = new SelectBox(boxProto->clone(), dialog, items);
  //widget->isFocusable = true;
  return widget;
}

// createSelectBox(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Choose Source", {})

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

//void MapsSources::onSceneLoaded()
//{
//  populateSceneVars();
//}

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

  Button* sourcesBtn = app->createPanelButton(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Sources");
  sourcesBtn->setMenu(sourcesMenu);
  sourcesBtn->onClicked = [this](){
    app->showPanel(sourcesPanel);
  };

  return sourcesBtn;
}
