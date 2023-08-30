#include "bookmarks.h"
#include "mapsapp.h"
#include "util.h"
#include "resources.h"

#include "sqlite3/sqlite3.h"
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
  const char* query = "INSERT INTO bookmarks (list_id,osm_id,title,props,notes,lng,lat,timestamp) VALUES (?,?,?,?,?,?,?,?);";
  DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
    sqlite3_bind_int(stmt, 1, list_id);
    sqlite3_bind_text(stmt, 2, osm_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, props, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, note, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 6, pos.longitude);
    sqlite3_bind_double(stmt, 7, pos.latitude);
    sqlite3_bind_int(stmt, 8, timestamp > 0 ? timestamp : int(mSecSinceEpoch()/1000));
  });

  bkmkPanelDirty = true;
  listsDirty = archiveDirty = true;  // list is dirty too since it shows number of bookmarks

  auto it = bkmkMarkers.find(list_id);
  if(it != bkmkMarkers.end()) {
    std::string propstr(props);
    std::string namestr(name);
    auto onPicked = [=](){ app->setPickResult(pos, namestr, propstr); };
    it->second->createMarker(pos, onPicked, {{{"name", name}}});
  }

  return sqlite3_last_insert_rowid(app->bkmkDB);
}

int MapsBookmarks::getListId(const char* listname, bool create)
{
  int list_id = -1;
  DB_exec(app->bkmkDB, "SELECT id FROM lists WHERE title = ?;",
      [&](sqlite3_stmt* stmt){ list_id = sqlite3_column_int(stmt, 0);},
      [&](sqlite3_stmt* stmt){ sqlite3_bind_text(stmt, 1, listname, -1, SQLITE_STATIC); });
  if(list_id < 0 && create) {
    const char* query = "INSERT INTO lists (title, color) VALUES (?,?);";
    DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, listname, -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 2, "#FF0000", -1, SQLITE_STATIC);
    });
    list_id = sqlite3_last_insert_rowid(app->bkmkDB);
  }
  return list_id;
}

static Widget* createNewListWidget(std::function<void(int, std::string)> callback)
{
  Widget* newListContent = createColumn();
  newListContent->node->setAttribute("box-anchor", "hfill");
  TextEdit* newListTitle = createTextEdit();
  ColorPicker* newListColor = createColorPicker(MapsApp::markerColors, Color::BLUE);
  Button* createListBtn = createPushbutton("Create");
  Button* cancelListBtn = createPushbutton("Cancel");
  createListBtn->onClicked = [=](){
    char colorstr[64];
    SvgWriter::serializeColor(colorstr, newListColor->color());
    std::string listname = newListTitle->text();
    if(listname.empty()) return;
    const char* query = "INSERT INTO lists (title, color) VALUES (?,?);";
    DB_exec(MapsApp::bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, listname.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, colorstr, -1, SQLITE_TRANSIENT);
    });
    int list_id = sqlite3_last_insert_rowid(MapsApp::bkmkDB);
    if(callback)
      callback(list_id, listname);
    newListContent->setVisible(false);
  };
  cancelListBtn->onClicked = [=](){ newListContent->setVisible(false); };
  newListTitle->onChanged = [=](const char* s){ createListBtn->setEnabled(s[0]); };
  newListContent->addWidget(createTitledRow("Title", newListTitle, newListColor));
  //newListContent->addWidget(createTitledRow("Color", newListColor));
  newListContent->addWidget(createTitledRow(NULL, createListBtn, cancelListBtn));
  newListContent->setVisible(false);
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

  //const char* query = "SELECT id, title FROM lists JOIN lists_state AS s ON lists.id = s.list_id WHERE lists.archived = 0 ORDER BY s.ordering;";
  const char* query = "SELECT id, title FROM lists WHERE archived = 0 ORDER BY id;";
  DB_exec(app->bkmkDB, query, [=](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    std::string listname = (const char*)(sqlite3_column_text(stmt, 1));
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

  dialog->setWinBounds(Rect::ltwh(400,400,500,700));

  app->gui->showModal(dialog, app->gui->windows.front()->modalOrSelf());
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

  const char* query = "SELECT lists.id, lists.title, lists.color, COUNT(1) FROM lists JOIN bookmarks AS b"
      " ON lists.id = b.list_id WHERE lists.archived = ? GROUP by lists.id;";
  DB_exec(app->bkmkDB, query, [=](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    std::string list = (const char*)(sqlite3_column_text(stmt, 1));
    std::string color = (const char*)(sqlite3_column_text(stmt, 2));
    int nplaces = sqlite3_column_int(stmt, 3);

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
      //std::string q1 = fstring("UPDATE lists_state SET visible = %d WHERE list_id = ?;", visible ? 1 : 0);
      //DB_exec(app->bkmkDB, q1.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
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
      DB_exec(app->bkmkDB, "UPDATE lists SET color = ? WHERE id = ?;", NULL, [&](sqlite3_stmt* stmt1){
        sqlite3_bind_text(stmt1, 1, buff, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt1, 2, rowid);
      });
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
      DB_exec(app->bkmkDB, q2.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
      if(archived) listsDirty = true; else archiveDirty = true;
      app->gui->deleteWidget(item);
    });

    // undo?  maybe try https://www.sqlite.org/undoredo.html (using triggers to populate an undo table)
    overflowMenu->addItem("Delete", [=](){
      const char* q = "DELETE FROM bookmarks WHERE list_id = ?;";
      DB_exec(app->bkmkDB, q, NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });

      const char* q1 = "DELETE FROM lists WHERE id = ?;";
      DB_exec(app->bkmkDB, q1, NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });

      auto it1 = bkmkMarkers.find(rowid);
      if(it1 != bkmkMarkers.end())
        bkmkMarkers.erase(it1);

      yamlRemove(app->config["places"]["visible"], rowid);

      if(archived) listsDirty = true;  // Deleteing archived item will dirty archive count
      app->gui->deleteWidget(item);
    });

    overflowMenu->addItem("Clear", [=](){
      const char* q = "DELETE FROM bookmarks WHERE list_id = ?;";
      DB_exec(app->bkmkDB, q, NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
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
  }, [&](sqlite3_stmt* stmt){
    sqlite3_bind_int(stmt, 1, archived ? 1 : 0);
  });

  if(!archived) {
    int narchived = 0;
    DB_exec(app->bkmkDB, "SELECT COUNT(1) FROM lists WHERE archived = 1;", [&narchived](sqlite3_stmt* stmt){
      narchived = sqlite3_column_int(stmt, 0);
    });

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

void MapsBookmarks::populateBkmks(int list_id, bool createUI)
{
  std::string listname;
  std::string color;
  DB_exec(app->bkmkDB, "SELECT title, color FROM lists WHERE id = ?;", [&](sqlite3_stmt* stmt){
    listname = (const char*)(sqlite3_column_text(stmt, 0));
    color = (const char*)(sqlite3_column_text(stmt, 1));
  }, [&](sqlite3_stmt* stmt){ sqlite3_bind_int(stmt, 1, list_id); });

  if(createUI) {
    bkmkPanelDirty = false;
    app->showPanel(bkmkPanel, true);
    app->gui->deleteContents(bkmkContent, ".listitem");
    bkmkPanel->selectFirst(".panel-title")->setText(listname.c_str());
    if(activeListId < 0)
      hideBookmarks(list_id);
    activeListId = list_id;
  }

  MarkerGroup* markerGroup = NULL;
  auto it = bkmkMarkers.find(list_id);
  if(it == bkmkMarkers.end()) {
    auto mg = std::make_unique<MarkerGroup>(app->map, "layers.bookmark-marker.draw.marker");
    markerGroup = bkmkMarkers.emplace(list_id, std::move(mg)).first->second.get();
    markerGroup->defaultVis = !createUI;
  }
  else
    it->second->setVisible(true);

  std::string srt = app->config["bookmarks"]["sort"].as<std::string>("date");
  std::string strStr = srt == "name" ? "title" : srt == "dist" ? "osmSearchRank(-1, lng, lat)" : "timestamp DESC";
  std::string query = "SELECT rowid, title, props, notes, lng, lat, timestamp FROM bookmarks WHERE list_id = ? ORDER BY " + strStr + ";";
  DB_exec(app->bkmkDB, query.c_str(), [&](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    std::string namestr = (const char*)(sqlite3_column_text(stmt, 1));
    std::string propstr = (const char*)(sqlite3_column_text(stmt, 2));
    const char* notestr = (const char*)(sqlite3_column_text(stmt, 3));
    double lng = sqlite3_column_double(stmt, 4);
    double lat = sqlite3_column_double(stmt, 5);
    int64_t timestamp = sqlite3_column_int64(stmt, 6);
    auto onPicked = [=](){ app->setPickResult(LngLat(lng, lat), namestr, propstr); };
    if(markerGroup)
      markerGroup->createMarker(LngLat(lng, lat), onPicked, {{{"name", namestr}, {"color", color}}});

    if(createUI) {
      Button* item = createListItem(MapsApp::uiIcon("pin"), namestr.c_str(), notestr);  //new Button(placeListProto->clone());
      item->onClicked = onPicked;  //[=](){ app->setPickResult(LngLat(lng, lat), namestr, propstr); };

      // alternative to overflow would be multi-select w/ selection toolbar; part of MapPanel, shared
      //  between bookmarks, tracks, etc
      Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");  //new Button(item->containerNode()->selectFirst(".overflow-btn"));
      Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
      overflowMenu->addItem("Delete", [=](){
        const char* q = "DELETE FROM bookmarks WHERE rowid = ?;";
        DB_exec(app->bkmkDB, q, NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
        listsDirty = archiveDirty = true;  // not worth the trouble of figuring out if list is archived or not
        app->gui->deleteWidget(item);
      });
      overflowMenu->addItem(timestamp > INT_MAX ? "Unpin" : "Pin", [=](){
        const char* q = "UPDATE bookmarks SET timestamp = ? WHERE rowid = ?;";
        int64_t newts = timestamp > INT_MAX ? timestamp - INT_MAX : timestamp + INT_MAX;
        DB_exec(app->bkmkDB, q, NULL, [&](sqlite3_stmt* stmt1){
          sqlite3_bind_int64(stmt1, 1, newts);
          sqlite3_bind_int(stmt1, 2, rowid);
        });
        populateBkmks(activeListId, true);  // actually only needed if sorted by date
      });
      overflowBtn->setMenu(overflowMenu);
      item->selectFirst(".child-container")->addWidget(overflowBtn);

      item->setUserData(LngLat(lng, lat));
      bkmkContent->addWidget(item);
    }
  }, [&](sqlite3_stmt* stmt){
    sqlite3_bind_int(stmt, 1, list_id);
  });
  if(createUI && mapAreaBkmks)
    onMapEvent(MAP_CHANGE);
}

void MapsBookmarks::onMapEvent(MapEvent_t event)
{
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
  const char* query1 = "SELECT rowid, osm_id, list_id, title, notes, lng, lat FROM bookmarks WHERE osm_id = ?;";
  DB_exec(app->bkmkDB, query1, [&](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    int listid = sqlite3_column_int(stmt, 2);
    const char* namestr = (const char*)(sqlite3_column_text(stmt, 3));
    const char* notestr = (const char*)(sqlite3_column_text(stmt, 4));
    content->addWidget(getPlaceInfoSubSection(rowid, listid, namestr, notestr));
  }, [&](sqlite3_stmt* stmt){
    sqlite3_bind_text(stmt, 1, osm_id.c_str(), -1, SQLITE_TRANSIENT);
  });

  if(content->containerNode()->children().empty()) {
    const char* query2 = "SELECT rowid, osm_id, list_id, title, notes, lng, lat FROM bookmarks WHERE "
        "lng >= ? AND lat >= ? AND lng <= ? AND lat <= ? LIMIT 1;";
    DB_exec(app->bkmkDB, query2, [&](sqlite3_stmt* stmt){
      if(osm_id.empty() || sqlite3_column_text(stmt, 1)[0] == '\0') {
        int rowid = sqlite3_column_int(stmt, 0);
        int listid = sqlite3_column_int(stmt, 2);
        const char* namestr = (const char*)(sqlite3_column_text(stmt, 3));
        const char* notestr = (const char*)(sqlite3_column_text(stmt, 4));
        content->addWidget(getPlaceInfoSubSection(rowid, listid, namestr, notestr));
      }
    }, [&](sqlite3_stmt* stmt){
      constexpr double delta = 0.00001;
      sqlite3_bind_double(stmt, 1, pos.longitude - delta);
      sqlite3_bind_double(stmt, 2, pos.latitude - delta);
      sqlite3_bind_double(stmt, 3, pos.longitude + delta);
      sqlite3_bind_double(stmt, 4, pos.latitude + delta);
    });
  }
  //if(content->containerNode()->children().empty())
  //  content->addWidget(getPlaceInfoSubSection(-1, -1, "", ""));  // for adding to bookmark list
  return content;
}

Widget* MapsBookmarks::getPlaceInfoSubSection(int rowid, int listid, std::string namestr, std::string notestr)
{
  std::string liststr;
  DB_exec(app->bkmkDB, "SELECT title FROM lists WHERE id = ?;", [&](sqlite3_stmt* stmt){
    liststr = (const char*)(sqlite3_column_text(stmt, 0));
  }, [&](sqlite3_stmt* stmt){ sqlite3_bind_int(stmt, 1, listid); });

  Widget* section = createColumn();
  section->node->setAttribute("box-anchor", "hfill");
  TextBox* noteText = new TextBox(loadSVGFragment(
      R"(<text class="note-text weak" box-anchor="left" margin="0 10" font-size="12"></text>)"));
  noteText->setText(notestr.c_str());
  noteText->setText(SvgPainter::breakText(static_cast<SvgText*>(noteText->node), 250).c_str());

  // bookmark editing
  auto editToolbar = createToolbar();
  auto titleEdit = createTextEdit();
  auto noteEdit = createTextEdit();
  auto acceptNoteBtn = createToolbutton(MapsApp::uiIcon("accept"));
  auto cancelNoteBtn = createToolbutton(MapsApp::uiIcon("cancel"));

  titleEdit->setText(namestr.c_str());
  noteEdit->setText(notestr.c_str());

  Widget* editContent = createColumn();
  editContent->addWidget(createTitledRow("Name", titleEdit));
  editContent->addWidget(createTitledRow("Note", noteEdit));
  editToolbar->addWidget(acceptNoteBtn);
  editToolbar->addWidget(cancelNoteBtn);
  editContent->addWidget(editToolbar);
  editContent->setVisible(false);

  acceptNoteBtn->onClicked = [=](){
    const char* q = "UPDATE bookmarks SET title = ?, notes = ? WHERE rowid = ?;";
    DB_exec(app->bkmkDB, q, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, titleEdit->text().c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, noteEdit->text().c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(stmt, 3, rowid);
    });
    // update title
    SvgText* titlenode = static_cast<SvgText*>(app->infoContent->containerNode()->selectFirst(".title-text"));
    titlenode->clearText();
    titlenode->addText(titleEdit->text().c_str());

    noteText->setText(noteEdit->text().c_str());
    editContent->setVisible(false);
    noteText->setVisible(true);
    bkmkPanelDirty = true;
  };

  cancelNoteBtn->onClicked = [=](){
    editContent->setVisible(false);
    noteText->setVisible(true);
  };

  Widget* toolRow = createRow();
  Button* chooseListBtn = createToolbutton(MapsApp::uiIcon("pin"), liststr.c_str(), true);
  Button* removeBtn = createToolbutton(MapsApp::uiIcon("discard"), "Delete");
  Button* addNoteBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit");

  //auto removeBtn = new Button(widget->containerNode()->selectFirst(".discard-btn"));
  removeBtn->onClicked = [=](){
    DB_exec(app->bkmkDB, "DELETE FROM bookmarks WHERE rowid = ?;", NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_int(stmt, 1, rowid);
    });
    bkmkPanelDirty = true;
    listsDirty = archiveDirty = true;  // list is dirty too since it shows number of bookmarks
    app->gui->deleteWidget(section);
  };

  //auto addNoteBtn = new Button(widget->containerNode()->selectFirst(".addnote-btn"));
  //addNoteBtn->setVisible(notestr.empty());
  addNoteBtn->onClicked = [=](){
    editContent->setVisible(true);
    app->gui->setFocused(noteEdit);
  };

  auto setListFn = [=](int list_id, std::string listname){
    DB_exec(app->bkmkDB, "UPDATE bookmarks SET list_id = ? WHERE rowid = ?;", NULL, [=](sqlite3_stmt* stmt1){
      sqlite3_bind_int(stmt1, 1, list_id);
      sqlite3_bind_int(stmt1, 2, rowid);
    });
    chooseListBtn->setText(listname.c_str());
  };

  //Button* chooseListBtn = new Button(widget->containerNode()->selectFirst(".combobox"));
  chooseListBtn->onClicked = [=](){
    chooseBookmarkList(setListFn);  //rowid);
  };

  toolRow->addWidget(chooseListBtn);
  toolRow->addWidget(createStretch());
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
  newListBtn->onClicked = [=](){ newListContent->setVisible(!newListContent->isVisible()); };

  listsContent = new DragDropList;  //
  Widget* listsCol = createColumn(); //createListContainer();
  listsCol->node->setAttribute("box-anchor", "fill");  // ancestors of ScrollWidget must use fill, not vfill
  listsCol->addWidget(newListContent);
  listsCol->addWidget(listsContent);
  auto listHeader = app->createPanelHeader(MapsApp::uiIcon("pin"), "Bookmark Lists");
  listsPanel = app->createMapPanel(listHeader, NULL, listsCol);

  archivedContent = createColumn();
  auto archivedHeader = app->createPanelHeader(MapsApp::uiIcon("archive"), "Archived Bookmaks");
  archivedPanel = app->createMapPanel(archivedHeader, archivedContent);

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
  bkmkContent = createColumn(); //createListContainer();
  Widget* editListContent = createColumn();
  TextEdit* listTitle = createTextEdit();
  TextEdit* listColor = createTextEdit();  // obviously needs to be replaced with a color picker
  Button* saveListBtn = createPushbutton("Apply");
  saveListBtn->onClicked = [=](){
    const char* query = "UPDATE lists SET title = ?, color = ? WHERE id = ?;";
    DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, listTitle->text().c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, listColor->text().c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(stmt, 3, activeListId);
    });
    listsDirty = archiveDirty = true;
  };
  editListContent->addWidget(createTitledRow("Title", listTitle));
  editListContent->addWidget(createTitledRow("Color", listColor));
  editListContent->addWidget(saveListBtn);
  editListContent->setVisible(false);
  bkmkContent->addWidget(editListContent);

  auto toolbar = app->createPanelHeader(MapsApp::uiIcon("folder"), "");
  Button* editListBtn = createToolbutton(MapsApp::uiIcon("edit"), "Edit List");
  editListBtn->onClicked = [=](){
    editListContent->setVisible(!editListContent->isVisible());
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
  mapAreaBkmksBtn->onClicked = [this](){
    mapAreaBkmks = !mapAreaBkmks;
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
  //DB_exec(app->bkmkDB, "SELECT list_id FROM lists_state WHERE visible = 1;", [&](sqlite3_stmt* stmt){
  //  populateBkmks(sqlite3_column_int(stmt, 0), false);
  //});
  YAML::Node vislists;
  Tangram::YamlPath("+places.visible").get(app->config, vislists);  //node = app->getConfigPath("+places.visible");
  for(auto& node : vislists)
    populateBkmks(node.as<int>(-1), false);

  Menu* bkmkMenu = createMenu(Menu::VERT_LEFT);
  //bkmkMenu->autoClose = true;
  bkmkMenu->addHandler([this, bkmkMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(bkmkMenu->selectFirst(".child-container"));
      const char* query = "SELECT b.rowid, b.title, b.props, b.notes, b.lng, b.lat FROM bookmarks AS b "
          "JOIN lists ON lists.id = b.list_id WHERE lists.archived = 0 ORDER BY timestamp LIMIT 8;";
      DB_exec(app->bkmkDB, query, [&](sqlite3_stmt* stmt){
        std::string namestr = (const char*)(sqlite3_column_text(stmt, 1));
        std::string propstr = (const char*)(sqlite3_column_text(stmt, 2));
        double lng = sqlite3_column_double(stmt, 4);
        double lat = sqlite3_column_double(stmt, 5);
        //const char* notestr = (const char*)(sqlite3_column_text(stmt, 2));
        bkmkMenu->addItem(namestr.c_str(), MapsApp::uiIcon("pin"), [=](){
          app->setPickResult(LngLat(lng, lat), namestr, propstr); });
      });
    }
    return false;
  });

  Button* bkmkBtn = app->createPanelButton(MapsApp::uiIcon("pin"), "Places", listsPanel);
  bkmkBtn->setMenu(bkmkMenu);
  return bkmkBtn;
}

// saved views?
// - does this really add much over a bookmark?  esp. if we can't choose a saved view fast than choosing a
//  bookmark + clearing marker and not important enough to deserve a top level panel
// - could provide option to save special bookmark type storing CameraPosition in props?

/*void MapsBookmarks::showViewsGUI()
{
  static int currViewIdx = 0;
  static std::string viewTitle;

  if (!ImGui::CollapsingHeader("Saved Views"))  //, ImGuiTreeNodeFlags_DefaultOpen))
    return;

  Map* map = app->map;
  // automatically suggest title based on city/state/country in view?
  bool ent = ImGui::InputText("Title", &viewTitle, ImGuiInputTextFlags_EnterReturnsTrue);
  if ((ImGui::Button("Save View") || ent) && !viewTitle.empty()) {
    const char* query = "REPLACE INTO saved_views (title,lng,lat,zoom,rotation,tilt,width,height) VALUES (?,?,?,?,?,?,?,?);";

    LngLat lngLatMin, lngLatMax;
    app->getMapBounds(lngLatMin, lngLatMax);
    auto view = map->getCameraPosition();

    DB_exec(bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, viewTitle.c_str(), -1, SQLITE_STATIC);
      sqlite3_bind_double(stmt, 2, view.longitude);
      sqlite3_bind_double(stmt, 3, view.latitude);
      sqlite3_bind_double(stmt, 4, view.zoom);
      sqlite3_bind_double(stmt, 5, view.rotation);
      sqlite3_bind_double(stmt, 6, view.tilt);
      sqlite3_bind_double(stmt, 7, lngLatMax.longitude - lngLatMin.longitude);
      sqlite3_bind_double(stmt, 8, lngLatMax.latitude - lngLatMin.latitude);
    });
  }

  std::vector<std::string> views;
  DB_exec(bkmkDB, "SELECT title FROM saved_views;", [&](sqlite3_stmt* stmt){
    views.emplace_back((const char*)(sqlite3_column_text(stmt, 0)));
  });

  std::vector<const char*> cviews;
  for(const auto& s : views)
    cviews.push_back(s.c_str());

  if(ImGui::ListBox("Views", &currViewIdx, cviews.data(), cviews.size())) {
    const char* query = "SELECT lng,lat,zoom,rotation,tilt FROM saved_views WHERE title = ?;";

    CameraPosition view;
    DB_exec(bkmkDB, query, [&](sqlite3_stmt* stmt){
      view.longitude = sqlite3_column_double(stmt, 0);
      view.latitude = sqlite3_column_double(stmt, 1);
      view.zoom = sqlite3_column_double(stmt, 2);
      view.rotation = sqlite3_column_double(stmt, 3);
      view.tilt = sqlite3_column_double(stmt, 4);
    }, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, views[currViewIdx].c_str(), -1, SQLITE_STATIC);
    });

    map->setCameraPositionEased(view, 1.0);
  }
}
*/
