#include "bookmarks.h"
#include "mapsapp.h"
#include "util.h"
#include "resources.h"

#include "sqlitepp.h"
#include "rapidjson/document.h"
#include "yaml-cpp/yaml.h"
#include "util/yamlPath.h"
#include "exif.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "usvg/svgpainter.h"
#include "usvg/svgparser.h"
#include "mapwidgets.h"
#include "gpxfile.h"
#include "offlinemaps.h"

// bookmarks (saved places)

int MapsBookmarks::addBookmark(int list_id, const std::string& osm_id, const std::string& name,
    const std::string& props, const std::string& note, LngLat pos, int timestamp)//, int rowid)
{
  if(timestamp <= 0) timestamp = int(mSecSinceEpoch()/1000);
  const char* query = "INSERT INTO bookmarks (list_id,osm_id,title,props,notes,lng,lat,timestamp) VALUES (?,?,?,?,?,?,?,?);";
  SQLiteStmt(app->bkmkDB, query).bind(list_id, osm_id, name, props, note, pos.longitude, pos.latitude, timestamp).exec();
  int rowid = sqlite3_last_insert_rowid(app->bkmkDB);

  bkmkPanelDirty = true;
  listsDirty = archiveDirty = true;  // list is dirty too since it shows number of bookmarks

  auto it = bkmkMarkers.find(list_id);
  if(it != bkmkMarkers.end()) {
    auto onPicked = [=](){ app->setPickResult(pos, name, props); };
    it->second->createMarker(pos, onPicked, {{{"name", name}}}, rowid);
  }

  return rowid;
}

int MapsBookmarks::getListId(const char* listname, bool create)
{
  int list_id = -1;
  SQLiteStmt(app->bkmkDB, "SELECT id FROM lists WHERE title = ?;").bind(listname).onerow(list_id);
  if(list_id < 0 && create) {
    SQLiteStmt(app->bkmkDB, "INSERT INTO lists (title, color) VALUES (?,?);")
        .bind(listname, colorToStr(nextListColor())).exec();
    list_id = sqlite3_last_insert_rowid(app->bkmkDB);
  }
  return list_id;
}

Color MapsBookmarks::nextListColor()
{
  // choose color from markerColors most distant from color of newest list (using hue for now)
  std::string prevcolor = "#12B5CB";
  SQLiteStmt(MapsApp::bkmkDB, "SELECT color FROM lists ORDER BY rowid DESC LIMIT 1;").onerow(prevcolor);
  float prevhue = ColorF(parseColor(prevcolor.c_str(), Color::CYAN)).hueHSV();
  float mindist = 1e6;
  size_t minidx = 0, ncolors = MapsApp::markerColors.size();
  for(size_t ii = 0; ii < ncolors; ++ii) {
    float d = std::abs(ColorF(MapsApp::markerColors[ii]).hueHSV() - prevhue);
    d = std::min(d, 360 - d);
    if(d < mindist) {
      mindist = d;
      minidx = ii;
    }
  }
  return MapsApp::markerColors[(minidx + ncolors/2 - 1)%ncolors];
}

Widget* MapsBookmarks::createNewListWidget(std::function<void(int, std::string)> callback)
{
  TextEdit* newListTitle = createTitledTextEdit("Title");
  ColorPicker* newListColor = createColorPicker(app->colorPickerMenu, Color::CYAN);
  Widget* newListRow = createRow();
  newListRow->addWidget(newListTitle);
  newListRow->addWidget(newListColor);
  newListColor->node->setAttribute("box-anchor", "bottom");
  auto onCreateList = [=](){
    std::string colorstr = colorToStr(newListColor->color());
    std::string listname = newListTitle->text();
    if(listname.empty()) return;
    SQLiteStmt(MapsApp::bkmkDB, "INSERT INTO lists (title, color) VALUES (?,?);").bind(listname, colorstr).exec();
    int list_id = sqlite3_last_insert_rowid(MapsApp::bkmkDB);
    if(callback)
      callback(list_id, listname);
  };
  auto newListContent = createInlineDialog({newListRow}, "Create", onCreateList);
  newListTitle->onChanged = [=](const char* s){ newListContent->selectFirst(".accept-btn")->setEnabled(s[0]); };
  newListContent->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      newListColor->setColor(nextListColor());
      newListTitle->setText("");
      gui->setFocused(newListTitle);
    }
    return false;
  });
  return newListContent;
}

void MapsBookmarks::chooseBookmarkList(std::function<void(int, std::string)> callback)  //int rowid)
{
  chooseListDialog.reset(createMobileDialog("Bookmark List", NULL));  //new Dialog( setupWindowNode(chooseListProto->clone()) );
  Widget* content = createColumn();
  content->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only

  Widget* newListContent = createNewListWidget([=](int id, std::string name){
    chooseListDialog->finish(Dialog::ACCEPTED);
    callback(id, name);
  });
  Button* newListBtn = createToolbutton(MapsApp::uiIcon("add-folder"), "Create List");
  newListBtn->onClicked = [=](){ newListContent->setVisible(!newListContent->isVisible()); };

  Toolbar* titleTb = static_cast<Toolbar*>(chooseListDialog->selectFirst(".title-toolbar"));
  titleTb->addWidget(newListBtn);

  const char* query = "SELECT id, title FROM lists WHERE archived = 0 ORDER BY id;";
  SQLiteStmt(app->bkmkDB, query).exec([&](int rowid, std::string listname){
    Button* item = createListItem(MapsApp::uiIcon("folder"), listname.c_str());  //new Button(listSelectProto->clone());
    item->onClicked = [=](){
      chooseListDialog->finish(Dialog::ACCEPTED);
      callback(rowid, listname);
    };
    content->addWidget(item);
  });

  Widget* dialogBody = chooseListDialog->selectFirst(".body-container");
  dialogBody->addWidget(newListContent);
  auto scrollWidget = new ScrollWidget(new SvgDocument(), content);
  scrollWidget->node->setAttribute("box-anchor", "fill");
  dialogBody->addWidget(scrollWidget);
  showModalCentered(chooseListDialog.get(), MapsApp::gui);
}

void MapsBookmarks::populateLists(bool archived)
{
  if(archived) archiveDirty = false; else listsDirty = false;

  std::vector<std::string> order;
  //Widget* content = archived ? archivedContent : listsContent;
  if(archived)
    app->gui->deleteContents(archivedContent, ".listitem");
  else {
    order = listsContent->getOrder();
    if(order.empty()) {
      for(const auto& key : app->config["places"]["list_order"])
        order.push_back(key.Scalar());
    }
    listsContent->clear();
  }

  // order by title for archived and by newest for main list to new lists not in sort order at top
  const char* query = "SELECT lists.id, lists.title, lists.color, COUNT(b.rowid) FROM lists LEFT JOIN"
      " bookmarks AS b ON lists.id = b.list_id WHERE lists.archived = ? GROUP by lists.id ORDER BY %s;";
  SQLiteStmt(app->bkmkDB, fstring(query, archived ? "lists.title" : "lists.rowid DESC")).bind(archived)
      .exec([=](int rowid, std::string list, std::string color, int nplaces){
    Button* item = createListItem(MapsApp::uiIcon("folder"),
        list.c_str(), nplaces == 1 ? "1 place" : fstring("%d places", nplaces).c_str());  //new Button(bkmkListProto->clone());
    item->onClicked = [=](){ populateBkmks(rowid, true); };
    Widget* container = item->selectFirst(".child-container");

    Button* showBtn = createToolbutton(MapsApp::uiIcon("eye"), "Show");
    container->addWidget(showBtn);
    auto it = bkmkMarkers.find(rowid);
    if(it != bkmkMarkers.end())
      showBtn->setChecked(it->second->defaultVis);

    showBtn->onClicked = [=](){
      bool visible = !showBtn->isChecked();
      showBtn->setChecked(visible);
      if(visible)
        app->config["places"]["visible"].push_back(rowid);
      else
        yamlRemove(app->config["places"]["visible"], rowid);
      auto it2 = bkmkMarkers.find(rowid);
      if(it2 == bkmkMarkers.end())
        populateBkmks(rowid, false);
      else {
        it2->second->defaultVis = visible;
        it2->second->setVisible(visible);
      }
    };

    ColorPicker* colorBtn = createColorPicker(app->colorPickerMenu, parseColor(color.c_str(), Color::CYAN));
    container->addWidget(colorBtn);
    colorBtn->onColor = [=](Color newcolor){
      std::string colorstr = colorToStr(newcolor);
      SQLiteStmt(app->bkmkDB, "UPDATE lists SET color = ? WHERE id = ?;").bind(colorstr, rowid).exec();
      auto it1 = bkmkMarkers.find(rowid);
      if(it1 != bkmkMarkers.end()) {
        bool vis = it1->second->defaultVis;
        if(vis)
          populateBkmks(rowid, false);
      }
    };

    Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
    container->addWidget(overflowBtn);
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowBtn->setMenu(overflowMenu);

    overflowMenu->addItem(archived ? "Unarchive" : "Archive", [=](){
      std::string q2 = fstring("UPDATE lists SET archived = %d WHERE id = ?;", archived ? 0 : 1);
      SQLiteStmt(app->bkmkDB, q2).bind(rowid).exec();
      populateLists(archived);  // count for "Archived" item has to be updated
    });

    // undo?  maybe try https://www.sqlite.org/undoredo.html (using triggers to populate an undo table)
    overflowMenu->addItem("Delete", [=](){
      SQLiteStmt(app->bkmkDB, "DELETE FROM bookmarks WHERE list_id = ?;").bind(rowid).exec();
      SQLiteStmt(app->bkmkDB, "DELETE FROM lists WHERE id = ?;").bind(rowid).exec();
      auto it1 = bkmkMarkers.find(rowid);
      if(it1 != bkmkMarkers.end())
        bkmkMarkers.erase(it1);
      yamlRemove(app->config["places"]["visible"], rowid);
      if(archived) listsDirty = true;  // deleting archived item will dirty archive count
      app->gui->deleteWidget(item);
    });

    if(archived)
      archivedContent->addWidget(item);
    else
      listsContent->addItem(std::to_string(rowid), item);
  //}, [&](sqlite3_stmt* stmt){
  //  sqlite3_bind_int(stmt, 1, archived ? 1 : 0);
  });

  if(!archived) {
    int narchived = 0;
    SQLiteStmt(app->bkmkDB, "SELECT COUNT(1) FROM lists WHERE archived = 1;").onerow(narchived);
    Button* item = createListItem(MapsApp::uiIcon("archive"), "Archived",
        narchived == 1 ? "1 list" : fstring("%d lists", narchived).c_str());  //new Button(bkmkListProto->clone());
    item->onClicked = [this](){ app->showPanel(archivedPanel, true); populateLists(true); };

    listsContent->addItem("archived", item);  //content->addWidget(item);
    listsContent->setOrder(order);
  }
}

void MapsBookmarks::hideBookmarks(int excludelist)
{
  for(auto& mg : bkmkMarkers) {
    if(mg.second->visible && mg.first != excludelist)
      mg.second->setVisible(false);
  }
}

void MapsBookmarks::restoreBookmarks()
{
  for(auto& mg : bkmkMarkers)
    mg.second->setVisible(mg.second->defaultVis);
}

void MapsBookmarks::deleteBookmark(int listid, int rowid)
{
  SQLiteStmt(app->bkmkDB, "DELETE FROM bookmarks WHERE rowid = ?;").bind(rowid).exec();
  listsDirty = archiveDirty = true;  // list is dirty too since it shows number of bookmarks
  auto it = bkmkMarkers.find(listid);
  if(it != bkmkMarkers.end())
    it->second->deleteMarker(rowid);
}

void MapsBookmarks::populateBkmks(int list_id, bool createUI)
{
  std::string listname;
  std::string color;
  SQLiteStmt(app->bkmkDB, "SELECT title, color FROM lists WHERE id = ?;").bind(list_id).onerow(listname, color);

  if(createUI) {
    bkmkPanelDirty = false;
    app->showPanel(bkmkPanel, true);
    app->gui->deleteContents(bkmkContent, ".listitem");
    static_cast<TextLabel*>(bkmkPanel->selectFirst(".panel-title"))->setText(listname.c_str());
    if(activeListId < 0)
      hideBookmarks(list_id);
    activeListId = list_id;
    activeListTitle = listname;
    activeListColor = color;
  }

  MarkerGroup* markerGroup = NULL;
  auto it = bkmkMarkers.find(list_id);
  if(it != bkmkMarkers.end()) {
    it->second->setVisible(true);
    if(it->second->commonProps.getString("color") != color) {
      markerGroup = it->second.get();
      markerGroup->reset();
    }
  }
  else {
    auto mg = std::make_unique<MarkerGroup>(app->map.get(),
        "layers.bookmark-marker.draw.marker", "layers.bookmark-dot.draw.marker");
    markerGroup = bkmkMarkers.emplace(list_id, std::move(mg)).first->second.get();
    markerGroup->defaultVis = !createUI;
  }
  if(markerGroup)
    markerGroup->commonProps = {{{"color", color}}};

  searchRankOrigin = app->currLocation.lngLat();
  std::string srt = app->config["bookmarks"]["sort"].as<std::string>("date");
  std::string strStr = srt == "name" ? "title" : srt == "dist" ? "osmSearchRank(-1.0, lng, lat)" : "timestamp DESC";
  std::string query = "SELECT rowid, title, props, notes, lng, lat, timestamp FROM bookmarks WHERE list_id = ? ORDER BY " + strStr + ";";
  SQLiteStmt(app->bkmkDB, query).bind(list_id).exec([&](int rowid, std::string namestr,
      std::string propstr, const char* notestr, double lng, double lat, int64_t timestamp){
    auto onPicked = [=](){ app->setPickResult(LngLat(lng, lat), namestr, propstr); };
    if(markerGroup)
      markerGroup->createMarker(LngLat(lng, lat), onPicked, {{{"name", namestr}}}, rowid);

    if(createUI) {
      // We allow empty name for marker to have marker w/o text on map, but need some text for list item
      const char* itemname = namestr.empty() ? "Untitled" : namestr.c_str();
      Button* item = createListItem(MapsApp::uiIcon("pin"), itemname, notestr);
      item->onClicked = onPicked;

      // alternative to overflow would be multi-select w/ selection toolbar; part of MapPanel, shared
      //  between bookmarks, tracks, etc
      Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
      Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
      overflowMenu->addItem("Delete", [=](){
        deleteBookmark(list_id, rowid);
        app->gui->deleteWidget(item);
      });
      overflowMenu->addItem(timestamp > INT_MAX ? "Unpin" : "Pin", [=](){
        int64_t newts = timestamp > INT_MAX ? timestamp - INT_MAX : timestamp + INT_MAX;
        SQLiteStmt(app->bkmkDB, "UPDATE bookmarks SET timestamp = ? WHERE rowid = ?;").bind(newts, rowid).exec();
        populateBkmks(activeListId, true);  // actually only needed if sorted by date
      });
      overflowBtn->setMenu(overflowMenu);
      item->selectFirst(".child-container")->addWidget(overflowBtn);

      item->setUserData(LngLat(lng, lat));
      bkmkContent->addWidget(item);
    }
  });
  if(createUI && mapAreaBkmks)
    onMapEvent(MAP_CHANGE);
}

void MapsBookmarks::onMapEvent(MapEvent_t event)
{
  if(event == MAP_CHANGE) {
    for(auto& mg : bkmkMarkers)
      mg.second->onZoom();
    if(mapAreaBkmks) {
      for(Widget* item : bkmkContent->select(".listitem")) {
        LngLat pos = item->userData<LngLat>();
        item->setVisible(app->map->lngLatToScreenPosition(pos.longitude, pos.latitude));
      }
    }
  }
  else if(event == MARKER_PICKED) {
    for(auto& mg : bkmkMarkers) {
      if(app->pickedMarkerId <= 0) return;
      if(mg.second->onPicked(app->pickedMarkerId))
        app->pickedMarkerId = 0;
    }
  }
  else if(event == SUSPEND) {
    std::vector<std::string> order = listsContent->getOrder();
    if(order.empty()) return;
    YAML::Node ordercfg = app->config["places"]["list_order"] = YAML::Node(YAML::NodeType::Sequence);
    for(const std::string& s : order)
      ordercfg.push_back(s);
  }
}

Widget* MapsBookmarks::getPlaceInfoSection(const std::string& osm_id, LngLat pos)
{
  Widget* content = createColumn();
  content->node->setAttribute("box-anchor", "hfill");
  content->node->addClass("bkmk-content");
  editPlaceDialogs.clear();
  // attempt lookup w/ osm_id if passed
  // - if no match, lookup by lat,lng but only accept hit w/o osm_id if osm_id is passed
  // - if this gives us too many false positives, we could add "Nearby bookmarks" title to section
  // - in the case of multiple bookmarks for a given osm_id, we just stack multiple subsections
  if(!osm_id.empty()) {
    const char* query1 = "SELECT rowid, list_id, title, notes FROM bookmarks WHERE osm_id = ?;";
    SQLiteStmt(app->bkmkDB, query1).bind(osm_id)
      .exec([&](int rowid, int listid, const char* namestr, const char* notestr){
        content->addWidget(getPlaceInfoSubSection(rowid, listid, namestr, notestr));
    });
  }
  if(content->containerNode()->children().empty()) {
    const char* query2 = "SELECT rowid, list_id, title, notes FROM bookmarks WHERE "
        "lng >= ? AND lat >= ? AND lng <= ? AND lat <= ? LIMIT 1;";
    constexpr double delta = 0.00001;
    SQLiteStmt(app->bkmkDB, query2)
        .bind(pos.longitude - delta, pos.latitude - delta, pos.longitude + delta, pos.latitude + delta)
        .exec([&](int rowid, int listid, const char* namestr, const char* notestr){
          content->addWidget(getPlaceInfoSubSection(rowid, listid, namestr, notestr));
    });
  }
  //if(content->containerNode()->children().empty())
  //  content->addWidget(getPlaceInfoSubSection(-1, -1, "", ""));  // for adding to bookmark list
  return content;
}

Widget* MapsBookmarks::getPlaceInfoSubSection(int rowid, int listid, std::string namestr, std::string notestr)
{
  std::string liststr;
  SQLiteStmt(app->bkmkDB, "SELECT title FROM lists WHERE id = ?;").bind(listid).onerow(liststr);

  Widget* section = createColumn();
  section->node->setAttribute("box-anchor", "hfill");
  TextBox* noteText = new TextBox(loadSVGFragment(
      R"(<text class="note-text weak" box-anchor="left" margin="0 10" font-size="12"></text>)"));
  noteText->setText(notestr.c_str());
  noteText->setText(SvgPainter::breakText(static_cast<SvgText*>(noteText->node), app->getPanelWidth() - 20).c_str());

  // bookmark editing
  auto titleEdit = createTitledTextEdit("Name");
  auto noteEdit = createTitledTextEdit("Note");
  titleEdit->setText(namestr.c_str());
  noteEdit->setText(notestr.c_str());

  auto onAcceptEdit = [=](){
    std::string title = titleEdit->text();
    SQLiteStmt(app->bkmkDB, "UPDATE bookmarks SET title = ?, notes = ? WHERE rowid = ?;")
        .bind(title, noteEdit->text(), rowid).exec();
    bkmkPanelDirty = true;
    auto it = bkmkMarkers.find(listid);
    if(it != bkmkMarkers.end())
      it->second->updateMarker(rowid, {{{"name", title}}});
    app->setPickResult(app->pickResultCoord, title, app->pickResultProps);  // must be last (destroys widget)
  };
  //auto onCancelEdit = [=](){ noteText->setVisible(true); };
  auto editPlaceDialog = createInputDialog({titleEdit, noteEdit}, "Edit Place", "Accept", onAcceptEdit);  //, onCancelEdit);
  editPlaceDialogs.emplace_back(editPlaceDialog);

  Widget* toolRow = createRow();
  //Button* chooseListBtn = createToolbutton(MapsApp::uiIcon("pin"), liststr.c_str(), true);
  // bit of a hack to use TextLabel on a toolbutton
  Button* chooseListBtn = new Button(widgetNode("#toolbutton"));
  chooseListBtn->setIcon(MapsApp::uiIcon("pin"));
  chooseListBtn->node->setAttribute("box-anchor", "hfill");
  TextLabel* listLabel = new TextLabel(chooseListBtn->containerNode()->selectFirst(".title"));
  listLabel->node->setAttribute("box-anchor", "hfill");
  listLabel->setVisible(true);
  listLabel->setText(liststr.c_str());

  Button* removeBtn = createToolbutton(MapsApp::uiIcon("discard"), "Delete");
  Button* addNoteBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit");

  removeBtn->onClicked = [=](){
    deleteBookmark(listid, rowid);
    bkmkPanelDirty = true;
    listsDirty = archiveDirty = true;  // list is dirty too since it shows number of bookmarks
    app->gui->deleteWidget(section);
  };

  //auto addNoteBtn = new Button(widget->containerNode()->selectFirst(".addnote-btn"));
  //addNoteBtn->setVisible(notestr.empty());
  addNoteBtn->onClicked = [=](){
    //noteText->setVisible(false);
    //showInlineDialogModal(editContent);
    showModalCentered(editPlaceDialog, MapsApp::gui);
    app->gui->setFocused(noteEdit, SvgGui::REASON_TAB);
  };

  auto setListFn = [=](int newlistid, std::string listname){
    if(newlistid == listid) return;
    SQLiteStmt(app->bkmkDB, "UPDATE bookmarks SET list_id = ? WHERE rowid = ?;").bind(newlistid, rowid).exec();
    // delete old marker
    auto oldit = bkmkMarkers.find(listid);
    if(oldit != bkmkMarkers.end())
      oldit->second->deleteMarker(rowid);
    // create new marker
    auto newit = bkmkMarkers.find(newlistid);
    if(newit != bkmkMarkers.end()) {
      LngLat pos;
      std::string name, props;
      std::string query = "SELECT title, props, lng, lat FROM bookmarks WHERE rowid = ?;";
      SQLiteStmt(app->bkmkDB, query).bind(rowid).onerow(name, props, pos.longitude, pos.latitude);
      auto onPicked = [=](){ app->setPickResult(pos, name, props); };
      newit->second->createMarker(pos, onPicked, {{{"name", name}}}, rowid);
    }
    bkmkPanelDirty = true;
    listsDirty = archiveDirty = true;
    // rebuild place info (listid, captured in several places, has changed)
    app->setPickResult(app->pickResultCoord, app->pickResultName, app->pickResultProps);
  };

  //Button* chooseListBtn = new Button(widget->containerNode()->selectFirst(".combobox"));
  chooseListBtn->onClicked = [=](){
    chooseBookmarkList(setListFn);  //rowid);
  };

  // long press to open list from a member
  chooseListBtn->addHandler([=](SvgGui* gui, SDL_Event* event){
    if(isLongPressOrRightClick(event)) {
      app->showPanel(listsPanel);
      app->panelToSkip = listsPanel;
      populateBkmks(listid, true);
      return true;
    }
    return false;
  });

  toolRow->addWidget(chooseListBtn);
  //toolRow->addWidget(createStretch());
  toolRow->addWidget(removeBtn);
  toolRow->addWidget(addNoteBtn);

  section->addWidget(toolRow);
  section->addWidget(noteText);
  //section->addWidget(editContent);

  return section;
}

void MapsBookmarks::addPlaceActions(Toolbar* tb)
{
  Button* createBkmkBtn = createToolbutton(MapsApp::uiIcon("add-pin"), "Add bookmark");
  //createBkmkBtn->node->setAttribute("box-anchor", "left");

  auto createBkmkFn = [=](int list_id, std::string listname){
    int rowid = addBookmark(list_id, app->pickResultOsmId,
        app->pickResultName, app->pickResultProps, "", app->pickResultCoord);
    Widget* section = getPlaceInfoSubSection(rowid, list_id, app->pickResultName, "");
    app->infoContent->selectFirst(".bkmk-content")->addWidget(section);
  };

  createBkmkBtn->onClicked = [=](){
    chooseBookmarkList(createBkmkFn);  //rowid);
  };

  tb->addWidget(createBkmkBtn);
}

void MapsBookmarks::importGpx(const char* filename)
{
  GpxFile gpx("", "", filename);
  loadGPX(&gpx);
  if(gpx.waypoints.empty()) {
    MapsApp::messageBox("Import bookmarks", fstring("No bookmarks found in %s", filename), {"OK"});
    return;
  }
  std::string style = gpx.style.empty() ? colorToStr(nextListColor()) : gpx.style;
  std::string title = gpx.title.empty() ? FSPath(filename).baseName() : gpx.title;
  SQLiteStmt(app->bkmkDB, "INSERT INTO lists (title, color) VALUES (?,?);").bind(title, style).exec();
  int64_t list_id = sqlite3_last_insert_rowid(app->bkmkDB);
  //if(timestamp <= 0) timestamp = int(mSecSinceEpoch()/1000);
  const char* query = "INSERT INTO bookmarks (list_id,osm_id,title,props,notes,lng,lat,timestamp) VALUES (?,?,?,?,?,?,?,?);";
  SQLiteStmt insbkmk(app->bkmkDB, query);
  for(auto& wpt : gpx.waypoints) {
    std::string osm_id = osmIdFromJson(strToJson(wpt.props.c_str()));
    if(wpt.name.empty())
      wpt.name = fstring("%.6f, %.6f", wpt.loc.lat, wpt.loc.lng);
    insbkmk.bind(list_id, osm_id, wpt.name, wpt.props, wpt.desc, wpt.loc.lng, wpt.loc.lat, int64_t(wpt.loc.time)).exec();
  }
  populateLists(false);
}

void MapsBookmarks::importImages(int64_t list_id, const char* path)
{
  std::vector<uint8_t> buf(2048);
  const char* query = "INSERT INTO bookmarks (list_id,osm_id,title,props,notes,lng,lat,timestamp) "
      "VALUES (?,?,?,?,?,?,?, CAST(strftime('%s', datetime(?)) AS INTEGER));";
  SQLiteStmt insbkmk(app->bkmkDB, query);
  //sqlite3_exec(app->bkmkDB, "BEGIN TRANSACTION", NULL, NULL, NULL);  -- would this lock DB for all threads?
  easyexif::EXIFInfo exif;
  exif.acceptTruncated = true;
  size_t nimages = 0;
  auto files = lsDirectory(path);
  std::sort(files.begin(), files.end());  // not really necessary since bookmarks never sorted by rowid
  for(auto& file : files) {
    FSPath fpath(path, file);
    auto ext = toLower(fpath.extension());
    if(ext != "jpg" && ext != "jpeg") continue;
    FileStream fs(fpath.c_str(), "rb");
    size_t len = fs.read(buf.data(), buf.size());
    int res = exif.parseFrom(buf.data(), len);
    if(res != 0) {
       LOGW("Error reading EXIF from %s (%d bytes): %d", fpath.c_str(), int(len), res);
       continue;
    }
    if(exif.GeoLocation.Latitude == 0 && exif.GeoLocation.Longitude == 0) continue;
    std::string date = exif.DateTime;
    // replace first two ':' with '-' to get proper date format
    for(size_t idx = 0, nrepl = 0; idx < date.size() && nrepl < 2; ++idx) {
      if(date[idx] == ':') { date[idx] = '-'; ++nrepl; }
    }
    std::string props = fstring(R"({"altitude": %.1f, "place_info":[{"icon":"", "title":"", "value":"<image href='%s' height='200'/>"}]})",
        exif.GeoLocation.Altitude, fpath.c_str());
    insbkmk.bind(list_id, 0, fpath.baseName(), props, "", exif.GeoLocation.Longitude, exif.GeoLocation.Latitude, date).exec();
    ++nimages;
  }
  std::string pathstr(path);
  MapsApp::runOnMainThread([=](){
    if(!nimages)
      MapsApp::messageBox("Import images", fstring("No geotagged images found in %s", pathstr.c_str()), {"OK"});
    else {
      listsDirty = true;
      if(activeListId == list_id)
        populateBkmks(activeListId, true);
      else if(listsPanel->isVisible())
        populateLists(false);
    }
  });
}

Button* MapsBookmarks::createPanel()
{
  // DB setup
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS lists(id INTEGER PRIMARY KEY, title TEXT NOT NULL,"
      " notes TEXT DEFAULT '', color TEXT NOT NULL, archived INTEGER DEFAULT 0);");
  //DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS lists_state(list_id INTEGER, ordering INTEGER, visible INTEGER);");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS bookmarks(osm_id TEXT, list_id INTEGER, title TEXT NOT NULL, props TEXT,"
      " notes TEXT NOT NULL, lng REAL, lat REAL, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));");
  //DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS saved_views(title TEXT UNIQUE, lng REAL, lat REAL, zoom REAL,"
  //    " rotation REAL, tilt REAL, width REAL, height REAL);");

  // Bookmark lists panel (main and archived lists)
  Widget* newListContent = createNewListWidget([this](int, std::string){ populateLists(false); });
  Button* newListBtn = createToolbutton(MapsApp::uiIcon("add-folder"), "Create List");
  newListBtn->onClicked = [=](){ showInlineDialogModal(newListContent); };

  listsContent = new DragDropList;  //
  Widget* listsCol = createColumn(); //createListContainer();
  listsCol->node->setAttribute("box-anchor", "fill");  // ancestors of ScrollWidget must use fill, not vfill
  listsCol->addWidget(newListContent);
  listsCol->addWidget(listsContent);
  auto listHeader = app->createPanelHeader(MapsApp::uiIcon("pin"), "Bookmark Lists");
  listHeader->addWidget(newListBtn);

  Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
  Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
  overflowBtn->setMenu(overflowMenu);
  overflowMenu->addItem("Import bookmarks", [=](){
    MapsApp::openFileDialog({{"GPX files", "gpx"}}, [this](const char* path){ importGpx(path); });
  });
  overflowMenu->addItem("Import photos", [=](){
    MapsApp::pickFolderDialog([this](const char* path){
      FSPath pathinfo(path);
      SQLiteStmt(app->bkmkDB, "INSERT INTO lists (title, color) VALUES (?,?);")
          .bind(pathinfo.baseName(), colorToStr(nextListColor())).exec();
      int64_t list_id = sqlite3_last_insert_rowid(app->bkmkDB);
      populateLists(false);
      MapsOffline::queueOfflineTask(0, [=](){ importImages(list_id, pathinfo.c_str()); });
    });
  });
  listHeader->addWidget(overflowBtn);

  listsPanel = app->createMapPanel(listHeader, NULL, listsCol, false);

  archivedContent = createColumn();
  auto archivedHeader = app->createPanelHeader(MapsApp::uiIcon("archive"), "Archived Bookmaks");
  archivedPanel = app->createMapPanel(archivedHeader, archivedContent, NULL, false);

  listsPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      if(listsDirty)
        populateLists(false);
    }
    return false;
  });

  archivedPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      if(archiveDirty)
        populateLists(true);
    }
    return false;
  });

  // Bookmarks panel
  bkmkContent = createColumn();
  TextEdit* listTitle = createTitledTextEdit("Title");
  ColorPicker* listColor = createColorPicker(app->colorPickerMenu, Color::CYAN);
  Widget* editListRow = createRow();
  editListRow->addWidget(listTitle);
  editListRow->addWidget(listColor);
  listColor->node->setAttribute("box-anchor", "bottom");
  auto onAcceptListEdit = [=](){
    SQLiteStmt(app->bkmkDB, "UPDATE lists SET title = ?, color = ? WHERE id = ?;").bind(
        listTitle->text(), colorToStr(listColor->color()), activeListId).exec();
    listsDirty = archiveDirty = true;
    // if bookmark list opened from place info, need to update list title there
    int listid = activeListId;
    if(!app->pickResultName.empty()) {
      app->popPanel();
      app->setPickResult(app->pickResultCoord, app->pickResultName, app->pickResultProps);
    }
    // update title and rebuild markers if color changed
    populateBkmks(listid, true);
  };
  auto editListContent = createInlineDialog({editListRow}, "Apply", onAcceptListEdit);
  bkmkContent->addWidget(editListContent);

  auto toolbar = app->createPanelHeader(MapsApp::uiIcon("folder"), "");

  static const char* bkmkSortKeys[] = {"name", "date", "dist"};
  std::string initSort = app->config["bookmarks"]["sort"].as<std::string>("date");
  size_t initSortIdx = 0;
  while(initSortIdx < 3 && initSort != bkmkSortKeys[initSortIdx]) ++initSortIdx;
  Menu* sortMenu = createRadioMenu({"Name", "Date", "Distance"}, [this](size_t ii){
    app->config["bookmarks"]["sort"] = bkmkSortKeys[ii];
    populateBkmks(activeListId, true);  // class member to hold current list name or id?
  }, initSortIdx);
  Button* sortBtn = createToolbutton(MapsApp::uiIcon("sort"), "Sort");
  sortBtn->setMenu(sortMenu);

  Button* mapAreaBkmksBtn = createToolbutton(MapsApp::uiIcon("fold-map-pin"), "Bookmarks in map area only");
  mapAreaBkmksBtn->onClicked = [=](){
    mapAreaBkmks = !mapAreaBkmks;
    mapAreaBkmksBtn->setChecked(mapAreaBkmks);
    if(mapAreaBkmks)
      onMapEvent(MAP_CHANGE);
    else {
      for(Widget* item : bkmkContent->select(".listitem"))
        item->setVisible(true);
    }
  };

  Button* listOverflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
  Menu* listOverflowMenu = createMenu(Menu::VERT_LEFT, false);
  listOverflowBtn->setMenu(listOverflowMenu);
  listOverflowMenu->addItem("Edit", [=](){
    bool show = !editListContent->isVisible();
    if(show) {
      listTitle->setText(activeListTitle.c_str());
      listColor->setColor(parseColor(activeListColor, Color::CYAN));
      MapsApp::gui->setFocused(listTitle, SvgGui::REASON_TAB);
      showInlineDialogModal(editListContent);
    }
  });

  listOverflowMenu->addItem("Clear", [=](){
    SQLiteStmt(app->bkmkDB, "DELETE FROM bookmarks WHERE list_id = ?;").bind(activeListId).exec();
    auto it1 = bkmkMarkers.find(activeListId);
    if(it1 != bkmkMarkers.end())
      it1->second->reset();
    populateBkmks(activeListId, true);
  });

  auto exportListFn = [this](const char* filename){
    GpxFile gpx(activeListTitle, "", filename);
    gpx.style = activeListColor;
    const char* q = "SELECT rowid, title, props, notes, lng, lat, timestamp FROM bookmarks WHERE list_id = ? ORDER BY timestamp DESC;";
    SQLiteStmt(app->bkmkDB, q).bind(activeListId).exec([&](int id, std::string namestr,
        std::string propstr, const char* notestr, double lng, double lat, int64_t timestamp){
      gpx.waypoints.push_back(Waypoint(Location{double(timestamp), lat, lng, 0,0,0,0,0,0,0}, namestr, notestr));
      gpx.waypoints.back().props = propstr;
    });
    saveGPX(&gpx);
  };
  listOverflowMenu->addItem("Export", [=](){
    // on Android, callback would be called with temp filename, then Android share dialog shown
    // ... should we saveFileDialog pass callback an IOStream instead of a filename?
    MapsApp::saveFileDialog(
        {{PLATFORM_MOBILE ? "application/gpx+xml" : "GPX file", "gpx"}}, activeListTitle, exportListFn);
  });


  toolbar->addWidget(sortBtn);
  toolbar->addWidget(mapAreaBkmksBtn);
  toolbar->addWidget(listOverflowBtn);
  bkmkPanel = app->createMapPanel(toolbar, bkmkContent);

  // depending on how minimize behavior is implemented, we may need something like this:
  //class MapPanel : public Widget { std::function<void()> onRestore; std::function<void()> onClose; };
  bkmkPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::VISIBLE) {
      if(bkmkPanelDirty)
        populateBkmks(activeListId, true);
    }
    else if(event->type == MapsApp::PANEL_CLOSED) {
      restoreBookmarks();
      activeListId = -1;
      //return true;
    }
    return false;
  });

  // handle visible bookmark lists
  YAML::Node vislists;
  Tangram::YamlPath("+places.visible").get(app->config, vislists);  //node = app->getConfigPath("+places.visible");
  for(auto& node : vislists)
    populateBkmks(node.as<int>(-1), false);

  Menu* bkmkMenu = createMenu(Menu::VERT);
  //bkmkMenu->autoClose = true;
  bkmkMenu->addHandler([this, bkmkMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(bkmkMenu->selectFirst(".child-container"));
      int uiWidth = app->getPanelWidth();
      const char* query = "SELECT b.title, b.props, b.lng, b.lat FROM bookmarks AS b JOIN lists ON "
          "lists.id = b.list_id WHERE lists.archived = 0 AND b.title <> '' ORDER BY timestamp DESC LIMIT 10;";
      SQLiteStmt(app->bkmkDB, query).exec([&](std::string name, std::string props, double lng, double lat){
        Button* item = bkmkMenu->addItem(name.c_str(), MapsApp::uiIcon("pin"),
            [=](){ app->setPickResult(LngLat(lng, lat), name, props); });
        SvgPainter::elideText(static_cast<SvgText*>(item->selectFirst(".title")->node), uiWidth - 100);
      });
    }
    return false;
  });

  Button* bkmkBtn = app->createPanelButton(MapsApp::uiIcon("pin"), "Places", listsPanel);
  bkmkBtn->setMenu(bkmkMenu);
  return bkmkBtn;
}
