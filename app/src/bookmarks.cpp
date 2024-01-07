#include "bookmarks.h"
#include "mapsapp.h"
#include "util.h"
#include "resources.h"

//#include "sqlite3/sqlite3.h"
#include "sqlitepp.h"
#include "rapidjson/document.h"
#include "yaml-cpp/yaml.h"
#include "util/yamlPath.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "usvg/svgpainter.h"
#include "usvg/svgwriter.h"
#include "usvg/svgparser.h"
#include "mapwidgets.h"

// bookmarks (saved places)

int MapsBookmarks::addBookmark(int list_id, const char* osm_id, const char* name, const char* props,
    const char* note, LngLat pos, int timestamp)//, int rowid)
{
  if(timestamp <= 0) timestamp = int(mSecSinceEpoch()/1000);
  const char* query = "INSERT INTO bookmarks (list_id,osm_id,title,props,notes,lng,lat,timestamp) VALUES (?,?,?,?,?,?,?,?);";
  SQLiteStmt(app->bkmkDB, query).bind(list_id, osm_id, name, props, note, pos.longitude, pos.latitude, timestamp).exec();
  int rowid = sqlite3_last_insert_rowid(app->bkmkDB);

  bkmkPanelDirty = true;
  listsDirty = archiveDirty = true;  // list is dirty too since it shows number of bookmarks

  auto it = bkmkMarkers.find(list_id);
  if(it != bkmkMarkers.end()) {
    std::string propstr(props);
    std::string namestr(name);
    auto onPicked = [=](){ app->setPickResult(pos, namestr, propstr); };
    it->second->createMarker(pos, onPicked, {{{"name", name}}}, rowid);
  }

  return rowid;
}

int MapsBookmarks::getListId(const char* listname, bool create)
{
  int list_id = -1;
  SQLiteStmt(app->bkmkDB, "SELECT id FROM lists WHERE title = ?;").bind(listname).onerow(list_id);
  if(list_id < 0 && create) {
    SQLiteStmt(app->bkmkDB, "INSERT INTO lists (title, color) VALUES (?,?);").bind(listname, "#FF0000").exec();
    list_id = sqlite3_last_insert_rowid(app->bkmkDB);
  }
  return list_id;
}

static Widget* createNewListWidget(std::function<void(int, std::string)> callback)
{
  TextEdit* newListTitle = createTitledTextEdit("Title");
  ColorPicker* newListColor = createColorPicker(MapsApp::markerColors, Color::BLUE);
  Widget* newListRow = createRow();
  newListRow->addWidget(newListTitle);
  newListRow->addWidget(newListColor);
  auto onCreateList = [=](){
    char colorstr[64];
    SvgWriter::serializeColor(colorstr, newListColor->color());
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
      newListColor->setColor(Color(0x12, 0xB5, 0xCB));
      newListTitle->setText("");
    }
    return false;
  });
  return newListContent;
}

void MapsBookmarks::chooseBookmarkList(std::function<void(int, std::string)> callback)  //int rowid)
{
  Dialog* dialog = new Dialog( setupWindowNode(chooseListProto->clone()) );
  Widget* content = createColumn();
  content->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only

  Widget* newListContent = createNewListWidget([=](int id, std::string name){
    dialog->finish(Dialog::ACCEPTED);
    callback(id, name);
  });
  Button* newListBtn = createToolbutton(MapsApp::uiIcon("add-folder"), "Create List");
  newListBtn->onClicked = [=](){ newListContent->setVisible(!newListContent->isVisible()); };

  Button* cancelBtn = createToolbutton(MapsApp::uiIcon("back"), "Cancel");
  cancelBtn->onClicked = [=](){
    dialog->finish(Dialog::CANCELLED);
  };

  const char* query = "SELECT id, title FROM lists WHERE archived = 0 ORDER BY id;";
  SQLiteStmt(app->bkmkDB, query).exec([&](int rowid, std::string listname){
    Button* item = createListItem(MapsApp::uiIcon("folder"), listname.c_str());  //new Button(listSelectProto->clone());
    item->onClicked = [=](){
      dialog->finish(Dialog::ACCEPTED);
      callback(rowid, listname);
    };
    content->addWidget(item);
  });

  TextBox* titleText = new TextBox(createTextNode("Bookmark List"));
  Toolbar* titleTb = createToolbar();
  titleTb->addWidget(cancelBtn);
  titleTb->addWidget(titleText);
  titleTb->addWidget(createStretch());
  titleTb->addWidget(newListBtn);
  dialog->selectFirst(".title-container")->addWidget(titleTb);

  Widget* dialogBody = dialog->selectFirst(".body-container");
  dialogBody->addWidget(newListContent);
  auto scrollWidget = new ScrollWidget(new SvgDocument(), content);
  scrollWidget->node->setAttribute("box-anchor", "fill");
  dialogBody->addWidget(scrollWidget);

  //dialog->setWinBounds(Rect::ltwh(400,400,500,700));
  //app->gui->showModal(dialog, app->gui->windows.front()->modalOrSelf());
  showModalCentered(dialog, MapsApp::gui);
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

  // order by title for archived - main list will use saved sort order
  const char* query = "SELECT lists.id, lists.title, lists.color, COUNT(1) FROM lists JOIN bookmarks AS b"
      " ON lists.id = b.list_id WHERE lists.archived = ? GROUP by lists.id ORDER BY lists.title;";
  SQLiteStmt(app->bkmkDB, query).bind(archived).exec([=](int rowid, std::string list, std::string color, int nplaces){
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

    ColorPicker* colorBtn = createColorPicker(app->markerColors, parseColor(color.c_str(), Color::CYAN));
    container->addWidget(colorBtn);
    colorBtn->onColor = [=](Color newcolor){
      char buff[64];
      SvgWriter::serializeColor(buff, newcolor);
      SQLiteStmt(app->bkmkDB, "UPDATE lists SET color = ? WHERE id = ?;").bind(buff, rowid).exec();
      auto it1 = bkmkMarkers.find(rowid);
      if(it1 != bkmkMarkers.end()) {
        bool vis = it1->second->defaultVis;
        bkmkMarkers.erase(it1);
        if(vis)
          populateBkmks(rowid, false);
      }
    };

    Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
    container->addWidget(overflowBtn);
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowMenu->addItem(archived ? "Unarchive" : "Archive", [=](){
      std::string q2 = fstring("UPDATE lists SET archived = %d WHERE id = ?;", archived ? 0 : 1);
      SQLiteStmt(app->bkmkDB, q2).bind(rowid).exec();
      if(archived) listsDirty = true; else archiveDirty = true;
      app->gui->deleteWidget(item);
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

    overflowMenu->addItem("Clear", [=](){
      SQLiteStmt(app->bkmkDB, "DELETE FROM bookmarks WHERE list_id = ?;").bind(rowid).exec();
      auto it1 = bkmkMarkers.find(rowid);
      if(it1 != bkmkMarkers.end())
        it1->second->reset();
      populateLists(archived);
    });

    // TODO: overflow menu option to generate track from bookmark list - just passes coords to MapsTracks
    overflowBtn->setMenu(overflowMenu);

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
  }

  MarkerGroup* markerGroup = NULL;
  auto it = bkmkMarkers.find(list_id);
  if(it == bkmkMarkers.end()) {
    auto mg = std::make_unique<MarkerGroup>(app->map.get(),
        "layers.bookmark-marker.draw.marker", "layers.bookmark-dot.draw.marker");
    markerGroup = bkmkMarkers.emplace(list_id, std::move(mg)).first->second.get();
    markerGroup->defaultVis = !createUI;
    markerGroup->commonProps = {{{"color", color}}};
  }
  else
    it->second->setVisible(true);

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
      Button* item = createListItem(MapsApp::uiIcon("pin"), namestr.c_str(), notestr);
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
  if(event == SUSPEND) {
    std::vector<std::string> order = listsContent->getOrder();
    if(order.empty()) return;
    YAML::Node ordercfg = app->config["places"]["list_order"] = YAML::Node(YAML::NodeType::Sequence);
    for(const std::string& s : order)
      ordercfg.push_back(s);
    return;
  }
  if(event != MAP_CHANGE)
    return;
  if(mapAreaBkmks) {
    for(Widget* item : bkmkContent->select(".listitem")) {
      LngLat pos = item->userData<LngLat>();
      item->setVisible(app->map->lngLatToScreenPosition(pos.longitude, pos.latitude));
    }
  }

  for(auto& mg : bkmkMarkers) {
    mg.second->onZoom();
    if(app->pickedMarkerId > 0) {
      if(mg.second->onPicked(app->pickedMarkerId))
        app->pickedMarkerId = 0;
    }
  }
}

Widget* MapsBookmarks::getPlaceInfoSection(const std::string& osm_id, LngLat pos)
{
  Widget* content = createColumn();
  content->node->setAttribute("box-anchor", "hfill");
  content->node->addClass("bkmk-content");
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
    // update title
    static_cast<TextLabel*>(app->infoPanel->selectFirst(".panel-title"))->setText(title.c_str());

    auto it = bkmkMarkers.find(listid);
    if(it != bkmkMarkers.end())
      it->second->updateMarker(rowid, {{{"name", title}}});

    noteText->setText(noteEdit->text().c_str());
    noteText->setVisible(true);
    bkmkPanelDirty = true;
  };
  auto onCancelEdit = [=](){ noteText->setVisible(true); };
  auto editContent = createInlineDialog({titleEdit, noteEdit}, "Apply", onAcceptEdit, onCancelEdit);

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
    app->gui->deleteWidget(section);
  };

  //auto addNoteBtn = new Button(widget->containerNode()->selectFirst(".addnote-btn"));
  //addNoteBtn->setVisible(notestr.empty());
  addNoteBtn->onClicked = [=](){
    noteText->setVisible(false);
    showInlineDialogModal(editContent);
    app->gui->setFocused(noteEdit);
  };

  auto setListFn = [=](int list_id, std::string listname){
    SQLiteStmt(app->bkmkDB, "UPDATE bookmarks SET list_id = ? WHERE rowid = ?;").bind(list_id, rowid).exec();
    chooseListBtn->setTitle(listname.c_str());
  };

  //Button* chooseListBtn = new Button(widget->containerNode()->selectFirst(".combobox"));
  chooseListBtn->onClicked = [=](){
    chooseBookmarkList(setListFn);  //rowid);
  };

  toolRow->addWidget(chooseListBtn);
  //toolRow->addWidget(createStretch());
  toolRow->addWidget(removeBtn);
  toolRow->addWidget(addNoteBtn);

  section->addWidget(toolRow);
  section->addWidget(noteText);
  section->addWidget(editContent);

  return section;
}

void MapsBookmarks::addPlaceActions(Toolbar* tb)
{
  Button* createBkmkBtn = createToolbutton(MapsApp::uiIcon("add-pin"), "Add bookmark");
  //createBkmkBtn->node->setAttribute("box-anchor", "left");

  auto createBkmkFn = [=](int list_id, std::string listname){
    rapidjson::Document& doc = app->pickResultProps;
    std::string title = doc.IsObject() && doc.HasMember("name") ?  doc["name"].GetString()
        : fstring("Pin: %.6f, %.6f", app->pickResultCoord.latitude, app->pickResultCoord.longitude);
    int rowid = addBookmark(list_id, osmIdFromProps(doc).c_str(), title.c_str(),
        rapidjsonToStr(doc).c_str(), "", app->pickResultCoord);

    Widget* section = getPlaceInfoSubSection(rowid, list_id, title.c_str(), "");
    app->infoContent->selectFirst(".bkmk-content")->addWidget(section);
  };

  createBkmkBtn->onClicked = [=](){
    chooseBookmarkList(createBkmkFn);  //rowid);
  };

  tb->addWidget(createBkmkBtn);
}

Button* MapsBookmarks::createPanel()
{
  static const char* chooseListProtoSVG = R"(
    <svg id="dialog" class="window dialog" layout="box">
      <rect class="dialog-bg background" box-anchor="fill" width="20" height="20"/>
      <g class="dialog-layout" box-anchor="fill" layout="flex" flex-direction="column">
        <g class="title-container" box-anchor="hfill" layout="box"></g>
        <rect class="hrule title" box-anchor="hfill" width="20" height="2"/>
        <g class="body-container" box-anchor="fill" layout="flex" flex-direction="column"></g>
      </g>
    </svg>
  )";
  chooseListProto.reset(static_cast<SvgDocument*>(loadSVGFragment(chooseListProtoSVG)));

  // DB setup
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS lists(id INTEGER PRIMARY KEY, title TEXT NOT NULL,"
      " notes TEXT DEFAULT '', color TEXT NOT NULL, archived INTEGER DEFAULT 0);");
  //DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS lists_state(list_id INTEGER, ordering INTEGER, visible INTEGER);");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS bookmarks(osm_id TEXT, list_id INTEGER, title TEXT NOT NULL, props TEXT,"
      " notes TEXT NOT NULL, lng REAL, lat REAL, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));");
  //DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS saved_views(title TEXT UNIQUE, lng REAL, lat REAL, zoom REAL,"
  //    " rotation REAL, tilt REAL, width REAL, height REAL);");

  // Bookmark lists panel (main and archived lists)
  Widget* newListContent = createNewListWidget({});
  Button* newListBtn = createToolbutton(MapsApp::uiIcon("add-folder"), "Create List");
  newListBtn->onClicked = [=](){ showInlineDialogModal(newListContent); };

  listsContent = new DragDropList;  //
  Widget* listsCol = createColumn(); //createListContainer();
  listsCol->node->setAttribute("box-anchor", "fill");  // ancestors of ScrollWidget must use fill, not vfill
  listsCol->addWidget(newListContent);
  listsCol->addWidget(listsContent);
  auto listHeader = app->createPanelHeader(MapsApp::uiIcon("pin"), "Bookmark Lists");
  listHeader->addWidget(newListBtn);
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
  ColorPicker* listColor = createColorPicker(app->markerColors, Color::CYAN);
  Widget* editListRow = createRow();
  editListRow->addWidget(listTitle);
  editListRow->addWidget(listColor);
  auto onAcceptListEdit = [=](){
    char colorstr[64];
    SvgWriter::serializeColor(colorstr, listColor->color());
    SQLiteStmt(app->bkmkDB, "UPDATE lists SET title = ?, color = ? WHERE id = ?;").bind(
        listTitle->text(), colorstr, activeListId).exec();
    listsDirty = archiveDirty = true;
  };
  auto editListContent = createInlineDialog({editListRow}, "Apply", onAcceptListEdit);
  bkmkContent->addWidget(editListContent);

  auto toolbar = app->createPanelHeader(MapsApp::uiIcon("folder"), "");
  Button* editListBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit List");
  editListBtn->onClicked = [=](){
    bool show = !editListContent->isVisible();
    if(show) {
      const char* query = "SELECT title, color FROM lists WHERE id = ?;";
      SQLiteStmt(app->bkmkDB, query).bind(activeListId).exec([&](const char* title, const char* colorstr){
        listTitle->setText(title);
        listColor->setColor(parseColor(colorstr, Color::CYAN));
      });
      showInlineDialogModal(editListContent);
    }
  };

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
  toolbar->addWidget(sortBtn);
  toolbar->addWidget(editListBtn);
  toolbar->addWidget(mapAreaBkmksBtn);
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
      const char* query = "SELECT b.title, b.props, b.lng, b.lat FROM bookmarks AS b "
          "JOIN lists ON lists.id = b.list_id WHERE lists.archived = 0 ORDER BY timestamp LIMIT 8;";
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
