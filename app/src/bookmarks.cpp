#include "bookmarks.h"
#include "mapsapp.h"
#include "util.h"
#include "resources.h"

#include "sqlite3/sqlite3.h"
#include "rapidjson/document.h"
#include "yaml-cpp/yaml.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "mapwidgets.h"

// bookmarks (saved places)

void MapsBookmarks::addBookmark(int list_id, const char* osm_id, const char* name, const char* props,
    const char* note, LngLat pos, int timestamp)//, int rowid)
{
  // WITH list_id AS (SELECT rowid FROM lists where title = ?)

  //rowid >= 0 ? "UPDATE bookmarks SET list_id = ?, osm_id = ?, title = ?, props = ?, notes = ?, lng = ?, lat = ? WHERE rowid = ?" :
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
    //if(rowid >= 0)
    //  sqlite3_bind_int(stmt, 9, rowid);
  });

  bkmkPanelDirty = true;
  //if(rowid >= 0)
  //  return;

  auto it = bkmkMarkers.find(list_id);
  if(it != bkmkMarkers.end()) {
    std::string propstr(props);
    std::string namestr(name);
    auto onPicked = [=](){ app->setPickResult(pos, namestr, propstr); };
    it->second->createMarker(pos, onPicked, {{{"name", name}}});
  }
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

void MapsBookmarks::chooseBookmarkList(std::function<void(int, std::string)> callback)  //int rowid)
{
  Dialog* dialog = new Dialog( setupWindowNode(chooseListProto->clone()) );
  Widget* content = createColumn();
  content->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only

  Widget* newListContent = createColumn();
  newListContent->node->setAttribute("box-anchor", "hfill");
  TextEdit* newListTitle = createTextEdit();
  TextEdit* newListColor = createTextEdit();  // obviously needs to be replaced with a color picker
  Button* createListBtn = createPushbutton("Create");
  createListBtn->onClicked = [=](){
    std::string listname = newListTitle->text();
    const char* query = "INSERT INTO lists (title, color) VALUES (?,?);";
    DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, listname.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, newListColor->text().c_str(), -1, SQLITE_TRANSIENT);
    });
    int list_id = sqlite3_last_insert_rowid(app->bkmkDB);
    dialog->finish(Dialog::ACCEPTED);
    callback(list_id, listname);
  };
  newListContent->addWidget(createTitledRow("Title", newListTitle));
  newListContent->addWidget(createTitledRow("Color", newListColor));
  newListContent->addWidget(createListBtn);
  newListContent->setVisible(false);

  Button* newListBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_add_folder.svg"), "Create List");
  newListBtn->onClicked = [=](){
    newListContent->setVisible(!newListContent->isVisible());
  };

  Button* cancelBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_back.svg"), "Cancel");
  cancelBtn->onClicked = [=](){
    dialog->finish(Dialog::CANCELLED);
  };

  //const char* query = "SELECT id, title FROM lists JOIN lists_state AS s ON lists.id = s.list_id WHERE lists.archived = 0 ORDER BY s.ordering;";
  const char* query = "SELECT id, title FROM lists WHERE archived = 0 ORDER BY id;";
  DB_exec(app->bkmkDB, query, [=](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    std::string listname = (const char*)(sqlite3_column_text(stmt, 1));
    Button* item = new Button(listSelectProto->clone());
    item->onClicked = [=](){
      dialog->finish(Dialog::ACCEPTED);
      callback(rowid, listname);
    };
    SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
    titlenode->addText(listname.c_str());
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

  app->showPanel(archived ? archivedPanel : listsPanel, archived);
  Widget* content = archived ? archivedContent : listsContent;
  app->gui->deleteContents(content, ".listitem");

  const char* query = "SELECT id, title FROM lists WHERE archived = ? ORDER BY id;";
  //const char* query = "SELECT id, title FROM lists "
  //    "JOIN lists_state AS s ON lists.id = s.list_id WHERE lists.archived = ? ORDER BY s.ordering;";
  //const char* query = "SELECT title, COUNT(1) FROM lists JOIN bookmarks AS b ON lists.id = b.list_id "
  //    "JOIN lists_state AS s ON lists.id = s.list_id WHERE lists.archived = ? GROUP by lists.row_id ORDER BY s.ordering;";
  DB_exec(app->bkmkDB, query, [=](sqlite3_stmt* stmt){  //"SELECT list, COUNT(1) FROM bookmarks GROUP by list;"
    int rowid = sqlite3_column_int(stmt, 0);
    std::string list = (const char*)(sqlite3_column_text(stmt, 1));
    int nplaces = 1000;  //sqlite3_column_int(stmt, 2);

    Button* item = new Button(bkmkListProto->clone());
    item->onClicked = [=](){ populateBkmks(rowid, true); };
    Button* dragBtn = new Button(item->containerNode()->selectFirst(".drag-btn"));
    dragBtn->addHandler([=](SvgGui* gui, SDL_Event* event){
      // if finger > height above or below center, shift position
      if(event->type == SDL_FINGERMOTION && gui->pressedWidget == dragBtn) {
        Rect b = dragBtn->node->bounds();
        real dy = event->tfinger.y - b.center().y;
        if(std::abs(dy) > b.height()) {
          SvgContainerNode* parent = item->parent()->containerNode();
          auto& items = parent->children();

          size_t ii = 0;
          auto it = items.begin();
          while(ii < items.size() && *it != item->node) { ++ii; ++it; }

          //auto it = std::find(items.begin(), items.end(), item);
          if(it == items.end() || (it == items.begin() && dy < 0)) return true;
          if(dy < 0)
            --it;
          // note iterator is advanced by 2 places and we assume archived lists item is always at end
          else if(++it == items.end() || ++it == items.end())
            return true;

          /*const char* q = "UPDATE lists_state SET ordering = (CASE ordering WHEN ?1 THEN ?2 WHEN ?2 THEN ?1) WHERE ordering in (?1, ?2);";
          DB_exec(app->bkmkDB, q, NULL, [&](sqlite3_stmt* stmt1){
            sqlite3_bind_int(stmt1, 1, ii);
            sqlite3_bind_int(stmt1, 1, dy < 0 ? ii-1 : ii+1);
          });*/

          SvgNode* next = *it;
          parent->removeChild(item->node);
          parent->addChild(item->node, next);
        }
        return true;
      }
      return false;
    });

    Button* showBtn = new Button(item->containerNode()->selectFirst(".visibility-btn"));
    auto it = bkmkMarkers.find(rowid);
    if(it != bkmkMarkers.end())
      showBtn->setChecked(it->second->visible);
    showBtn->onClicked = [=](){
      bool visible = !showBtn->isChecked();
      showBtn->setChecked(visible);

      //std::string q1 = fstring("UPDATE lists_state SET visible = %d WHERE list_id = ?;", visible ? 1 : 0);
      //DB_exec(app->bkmkDB, q1.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });

      if(bkmkMarkers.find(rowid) == bkmkMarkers.end())
        populateBkmks(rowid, false);
      else
        it->second->setVisible(visible);  //!it->second->visible);
    };

    Button* overflowBtn = new Button(item->containerNode()->selectFirst(".overflow-btn"));
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowMenu->addItem(archived ? "Unarchive" : "Archive", [=](){

      //std::string q1 = fstring("UPDATE lists_state SET ordering = (SELECT COUNT(1) FROM lists WHERE archived = %d) WHERE list_id = ?;", archived ? 0 : 1);
      //DB_exec(app->bkmkDB, q1.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });

      std::string q2 = fstring("UPDATE lists SET archived = %d WHERE id = ?;", archived ? 0 : 1);
      DB_exec(app->bkmkDB, q2.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
      app->gui->deleteWidget(item);
      if(archived) listsDirty = true; else archiveDirty = true;
    });

    // undo?  maybe try https://www.sqlite.org/undoredo.html (using triggers to populate an undo table)
    overflowMenu->addItem("Delete", [=](){
      const char* q = "DELETE FROM bookmarks WHERE list_id = ?";
      DB_exec(app->bkmkDB, q, NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });

      const char* q1 = "DELETE FROM lists WHERE id = ?";
      DB_exec(app->bkmkDB, q1, NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });

      auto it1 = bkmkMarkers.find(rowid);
      if(it1 != bkmkMarkers.end())
        bkmkMarkers.erase(it1);

      app->gui->deleteWidget(item);
      if(archived) listsDirty = true; else archiveDirty = true;
    });

    // TODO: overflow menu option to generate track from bookmark list - just passes coords to MapsTracks
    overflowBtn->setMenu(overflowMenu);

    SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
    titlenode->addText(list.c_str());

    SvgText* detailnode = static_cast<SvgText*>(item->containerNode()->selectFirst(".detail-text"));
    detailnode->addText(nplaces == 1 ? "1 place" : fstring("%d places", nplaces).c_str());

    content->addWidget(item);
  }, [&](sqlite3_stmt* stmt){
    sqlite3_bind_int(stmt, 1, archived ? 1 : 0);
  });

  if(!archived) {
    int narchived = 0;
    DB_exec(app->bkmkDB, "SELECT COUNT(1) FROM lists WHERE archived = 1;", [&narchived](sqlite3_stmt* stmt){
      narchived = sqlite3_column_int(stmt, 0);
    });

    Button* item = new Button(bkmkListProto->clone());
    item->onClicked = [this](){
      populateLists(true);
    };

    Button* showBtn = new Button(item->containerNode()->selectFirst(".visibility-btn"));
    showBtn->setVisible(false);
    Button* overflowBtn = new Button(item->containerNode()->selectFirst(".overflow-btn"));
    overflowBtn->setVisible(false);

    SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
    titlenode->addText("Archived");

    SvgText* detailnode = static_cast<SvgText*>(item->containerNode()->selectFirst(".detail-text"));
    detailnode->addText(narchived == 1 ? "1 list" : fstring("%d lists", narchived).c_str());

    content->addWidget(item);
  }
}

void MapsBookmarks::hideBookmarks(int excludelist)
{
  if(hiddenGroups.empty()) {
    for(auto& mg : bkmkMarkers) {
      if(mg.second->visible && mg.first != excludelist) {
        mg.second->setVisible(false);
        hiddenGroups.push_back(mg.second.get());
      }
    }
  }
}

void MapsBookmarks::restoreBookmarks()
{
  for(MarkerGroup* mg : hiddenGroups)
    mg->setVisible(true);
  hiddenGroups.clear();
}

void MapsBookmarks::populateBkmks(int list_id, bool createUI)
{
  std::string listname;
  DB_exec(app->bkmkDB, "SELECT title FROM lists WHERE id = ?;", [&](sqlite3_stmt* stmt){
    listname = (const char*)(sqlite3_column_text(stmt, 0));
  }, [&](sqlite3_stmt* stmt){ sqlite3_bind_int(stmt, 1, list_id); });

  if(createUI) {
    bkmkPanelDirty = false;
    app->showPanel(bkmkPanel);
    app->gui->deleteContents(bkmkContent, ".listitem");
    bkmkPanel->selectFirst(".panel-title")->setText(listname.c_str());
    if(activeListId < 0)  //hiddenGroups.empty())
      hideBookmarks(list_id);
    activeListId = list_id;
  }

  MarkerGroup* markerGroup = NULL;
  auto it = bkmkMarkers.find(list_id);
  if(it == bkmkMarkers.end()) {
    auto mg = std::make_unique<MarkerGroup>(app->map, "layers.bookmark-marker.draw.marker");
    markerGroup = bkmkMarkers.emplace(list_id, std::move(mg)).first->second.get();
  }

  std::string srt = app->config["bookmarks"]["sort"].as<std::string>("date");
  std::string strStr = srt == "name" ? "title" : srt == "dist" ? "osmSearchRank(-1, lng, lat)" : "timestamp DESC";
  std::string query = "SELECT rowid, title, props, notes, lng, lat FROM bookmarks WHERE list_id = ? ORDER BY " + strStr + ";";
  DB_exec(app->bkmkDB, query.c_str(), [&](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    std::string namestr = (const char*)(sqlite3_column_text(stmt, 1));
    std::string propstr = (const char*)(sqlite3_column_text(stmt, 2));
    const char* notestr = (const char*)(sqlite3_column_text(stmt, 3));
    double lng = sqlite3_column_double(stmt, 4);
    double lat = sqlite3_column_double(stmt, 5);
    auto onPicked = [=](){ app->setPickResult(LngLat(lng, lat), namestr, propstr); };
    if(markerGroup)
      markerGroup->createMarker(LngLat(lng, lat), onPicked, {{{"name", namestr}}});

    if(createUI) {
      Button* item = new Button(placeListProto->clone());
      item->onClicked = onPicked;  //[=](){ app->setPickResult(LngLat(lng, lat), namestr, propstr); };

      // alternative to overflow would be multi-select w/ selection toolbar; part of MapPanel, shared
      //  between bookmarks, tracks, etc
      Button* overflowBtn = new Button(item->containerNode()->selectFirst(".overflow-btn"));
      Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
      overflowMenu->addItem("Delete", [=](){
        const char* q = "DELETE FROM bookmarks WHERE rowid = ?";
        DB_exec(app->bkmkDB, q, NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
        app->gui->deleteWidget(item);
        listsDirty = archiveDirty = true;  // not worth the trouble if figuring out if list is archived or not
      });
      overflowBtn->setMenu(overflowMenu);

      SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
      titlenode->addText(namestr.c_str());
      SvgText* notenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".note-text"));
      notenode->addText(notestr);
      item->setUserData(LngLat(lng, lat));
      bkmkContent->addWidget(item);
    }
  }, [&](sqlite3_stmt* stmt){
    //sqlite3_bind_text(stmt, 1, listname.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 1, list_id);
  });
  // hide unused markers
  //for(; markerIdx < bkmkMarkers.size(); ++markerIdx)
  //  map->markerSetVisible(bkmkMarkers[markerIdx], false);
  // if requested, hide list entries for bookmarks not visible
  if(createUI && mapAreaBkmks)
    onMapChange();
}

void MapsBookmarks::onMapChange()
{
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
  // attempt lookup w/ osm_id if passed
  // - if no match, lookup by lat,lng but only accept hit w/o osm_id if osm_id is passed
  // - if this gives us too many false positives, we could add "Nearby bookmarks" title to section
  // - in the case of multiple bookmarks for a given osm_id, we just stack multiple subsections
  const char* query1 = "SELECT rowid, osm_id, list_id, props, notes, lng, lat FROM bookmarks WHERE osm_id = ?;";
  DB_exec(app->bkmkDB, query1, [&](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    int listid = sqlite3_column_int(stmt, 2);
    const char* notestr = (const char*)(sqlite3_column_text(stmt, 4));
    content->addWidget(getPlaceInfoSubSection(rowid, listid, notestr));
  }, [&](sqlite3_stmt* stmt){
    sqlite3_bind_text(stmt, 1, osm_id.c_str(), -1, SQLITE_TRANSIENT);
  });

  if(content->containerNode()->children().empty()) {
    const char* query2 = "SELECT rowid, osm_id, list_id, props, notes, lng, lat FROM bookmarks WHERE "
        "lng >= ? AND lat >= ? AND lng <= ? AND lat <= ? LIMIT 1;";
    DB_exec(app->bkmkDB, query2, [&](sqlite3_stmt* stmt){
      if(osm_id.empty() || sqlite3_column_text(stmt, 1)[0] == '\0') {
        int rowid = sqlite3_column_int(stmt, 0);
        int listid = sqlite3_column_int(stmt, 2);
        const char* notestr = (const char*)(sqlite3_column_text(stmt, 4));
        content->addWidget(getPlaceInfoSubSection(rowid, listid, notestr));
      }
    }, [&](sqlite3_stmt* stmt){
      constexpr double delta = 0.00001;
      sqlite3_bind_double(stmt, 1, pos.longitude - delta);
      sqlite3_bind_double(stmt, 2, pos.latitude - delta);
      sqlite3_bind_double(stmt, 3, pos.longitude + delta);
      sqlite3_bind_double(stmt, 4, pos.latitude + delta);
    });
  }

  if(content->containerNode()->children().empty())
    content->addWidget(getPlaceInfoSubSection(-1, -1, ""));  // for adding to bookmark list

  return content;
}

Widget* MapsBookmarks::getPlaceInfoSubSection(int rowid, int listid, std::string notestr)
{
  std::string liststr;
  DB_exec(app->bkmkDB, "SELECT title FROM lists WHERE id = ?;", [&](sqlite3_stmt* stmt){
    liststr = (const char*)(sqlite3_column_text(stmt, 0));
  }, [&](sqlite3_stmt* stmt){ sqlite3_bind_int(stmt, 1, listid); });

  Widget* widget = new Widget(placeInfoSectionProto->clone());
  TextBox* listText = new TextBox(static_cast<SvgText*>(widget->containerNode()->selectFirst(".list-name-text")));
  TextBox* noteText = new TextBox(static_cast<SvgText*>(widget->containerNode()->selectFirst(".note-text")));
  listText->setText(liststr.c_str());
  noteText->setText(notestr.c_str());

  Widget* bkmkStack = widget->selectFirst(".bkmk-display-container");
  Button* createBkmkBtn = new Button(widget->containerNode()->selectFirst(".addbkmk-btn"));
  // bookmark editing
  auto editToolbar = createToolbar();
  auto titleEdit = createTextEdit();
  auto noteEdit = createTextEdit();
  auto acceptNoteBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_accept.svg"));
  auto cancelNoteBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_cancel.svg"));

  Widget* editContent = createColumn();
  widget->selectFirst(".bkmk-edit-container")->addWidget(editContent);
  editContent->addWidget(titleEdit);
  editContent->addWidget(noteEdit);
  editToolbar->addWidget(acceptNoteBtn);
  editToolbar->addWidget(cancelNoteBtn);
  editContent->addWidget(editToolbar);
  editContent->setVisible(false);

  acceptNoteBtn->onClicked = [=](){
    const char* q = "UPDATE bookmarks SET title = ?, notes = ? WHERE rowid = ?";
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
  };

  cancelNoteBtn->onClicked = [=](){
    editContent->setVisible(false);
    noteText->setVisible(true);
  };

  auto removeBtn = new Button(widget->containerNode()->selectFirst(".discard-btn"));
  removeBtn->onClicked = [=](){
    DB_exec(app->bkmkDB, "DELETE FROM bookmarks WHERE rowid = ?;", NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_int(stmt, 1, rowid);
    });
    editContent->setVisible(false);  // note that we do not redisplay info stack
    noteText->setVisible(false);
    createBkmkBtn->setVisible(true);
    bkmkStack->setVisible(false);
  };

  auto addNoteBtn = new Button(widget->containerNode()->selectFirst(".addnote-btn"));
  //addNoteBtn->setVisible(notestr.empty());
  addNoteBtn->onClicked = [=](){
    editContent->setVisible(true);
    app->gui->setFocused(noteEdit);
  };

  auto setListFn = [=](int list_id, std::string listname){
    DB_exec(app->bkmkDB, "UPDATE bookmarks SET list_id = ? WHERE rowid = ?", NULL, [=](sqlite3_stmt* stmt1){
      sqlite3_bind_int(stmt1, 1, list_id);
      sqlite3_bind_int(stmt1, 2, rowid);
    });
    listText->setText(listname.c_str());
  };

  Button* chooseListBtn = new Button(widget->containerNode()->selectFirst(".combobox"));
  chooseListBtn->onClicked = [=](){
    chooseBookmarkList(setListFn);  //rowid);
  };

  auto createBkmkFn = [=](int list_id, std::string listname){
    rapidjson::Document& doc = app->pickResultProps;
    std::string title = doc.IsObject() && doc.HasMember("name") ?  doc["name"].GetString()
        : fstring("Pin: %.6f, %.6f", app->pickResultCoord.latitude, app->pickResultCoord.longitude);
    addBookmark(list_id, osmIdFromProps(doc).c_str(), title.c_str(),
        rapidjsonToStr(doc).c_str(), "", app->pickResultCoord);
    listText->setText(listname.c_str());
    createBkmkBtn->setVisible(false);
    bkmkStack->setVisible(true);
  };

  createBkmkBtn->onClicked = [=](){
    chooseBookmarkList(createBkmkFn);  //rowid);
  };

  createBkmkBtn->setVisible(rowid < 0);
  bkmkStack->setVisible(rowid >= 0);
  return widget;
}

Widget* MapsBookmarks::createPanel()
{
  static const char* bkmkListProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g layout="flex" flex-direction="row" box-anchor="left">
        <g class="toolbutton drag-btn" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_drawer.svg"/>
        </g>
        <g layout="box" box-anchor="vfill">
          <text class="title-text" box-anchor="left" margin="0 10"></text>
          <text class="detail-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
        </g>

        <rect class="stretch" fill="none" box-anchor="fill" width="20" height="20"/>

        <g class="toolbutton visibility-btn" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_pin.svg"/>
        </g>

        <g class="toolbutton overflow-btn" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_overflow.svg"/>
        </g>

      </g>
    </g>
  )";
  bkmkListProto.reset(loadSVGFragment(bkmkListProtoSVG));

  static const char* listSelectProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g layout="flex" flex-direction="row" box-anchor="left">
        <g class="image-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_drawer.svg"/>
        </g>
        <g layout="box" box-anchor="vfill">
          <text class="title-text" box-anchor="left" margin="0 10"></text>
        </g>
      </g>
    </g>
  )";
  listSelectProto.reset(loadSVGFragment(listSelectProtoSVG));

  static const char* placeListProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g layout="flex" flex-direction="row" box-anchor="left">
        <g class="image-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_bookmark.svg"/>
        </g>
        <g layout="box" box-anchor="vfill">
          <text class="title-text" box-anchor="left" margin="0 10"></text>
          <text class="note-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
        </g>

        <rect class="stretch" fill="none" box-anchor="fill" width="20" height="20"/>

        <g class="toolbutton overflow-btn" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_overflow.svg"/>
        </g>
      </g>
    </g>
  )";
  placeListProto.reset(loadSVGFragment(placeListProtoSVG));

  static const char* placeInfoSectionProtoSVG = R"(
    <g margin="0 5" box-anchor="hfill" layout="flex" flex-direction="column">
      <g class="pushbutton addbkmk-btn" margin="2 5">
        <text>Add Bookmark</text>
      </g>
      <g class="bkmk-display-container" layout="flex" flex-direction="row" box-anchor="left">
        <g class="bkmk-icon-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_bookmark.svg"/>
        </g>
        <g class="inputbox combobox" layout="box" box-anchor="left" margin="0 10">
          <rect class="min-width-rect" width="150" height="36" fill="none"/>
          <rect class="inputbox-bg" box-anchor="fill" width="150" height="36"/>

          <g class="combo_content" box-anchor="fill" layout="flex" flex-direction="row" margin="0 2">
            <g class="textbox combo_text" box-anchor="fill" layout="box">
              <text class="list-name-text"></text>
            </g>

            <g class="combo_open" box-anchor="vfill" layout="box">
              <rect fill="none" box-anchor="vfill" width="28" height="28"/>
              <use class="icon" width="28" height="28" xlink:href=":/icons/chevron_down.svg" />
            </g>
          </g>
        </g>
        <g class="toolbutton discard-btn" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_discard.svg"/>
        </g>
        <g class="pushbutton addnote-btn" margin="2 5">
          <text>Edit</text>
        </g>
      </g>
      <g class="bkmk-edit-container" box-anchor="hfill" layout="box"></g>
      <text class="note-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
    </g>
  )";
  placeInfoSectionProto.reset(loadSVGFragment(placeInfoSectionProtoSVG));

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
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS lists_state(list_id INTEGER, ordering INTEGER, visible INTEGER);");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS bookmarks(osm_id TEXT, list_id INTEGER, title TEXT NOT NULL, props TEXT,"
      " notes TEXT NOT NULL, lng REAL, lat REAL, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS saved_views(title TEXT UNIQUE, lng REAL, lat REAL, zoom REAL,"
      " rotation REAL, tilt REAL, width REAL, height REAL);");

  // Bookmark lists panel (main and archived lists)
  listsContent = createColumn(); //createListContainer();
  auto listHeader = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Bookmark Lists");
  listsPanel = app->createMapPanel(listHeader, listsContent);

  Widget* newListContent = createColumn();
  TextEdit* newListTitle = createTextEdit();
  TextEdit* newListColor = createTextEdit();  // obviously needs to be replaced with a color picker
  Button* createListBtn = createPushbutton("Create");
  createListBtn->onClicked = [=](){
    const char* query = "INSERT INTO lists (title, color) VALUES (?,?);";
    DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, newListTitle->text().c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, newListColor->text().c_str(), -1, SQLITE_TRANSIENT);
    });
  };
  newListContent->addWidget(createTitledRow("Title", newListTitle));
  newListContent->addWidget(createTitledRow("Color", newListColor));
  newListContent->addWidget(createListBtn);
  newListContent->setVisible(false);
  listsContent->addWidget(newListContent);

  Button* newListBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_add_folder.svg"), "Create List");
  newListBtn->onClicked = [=](){
    newListContent->setVisible(!newListContent->isVisible());
  };

  archivedContent = createColumn();
  auto archivedHeader = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Archived Bookmaks");
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
    const char* query = "UPDATE lists SET title = ?, color = ? WHERE id = ?";
    DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, listTitle->text().c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, listColor->text().c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(stmt, 3, activeListId);
    });
  };
  editListContent->addWidget(createTitledRow("Title", listTitle));
  editListContent->addWidget(createTitledRow("Color", listColor));
  editListContent->addWidget(saveListBtn);
  editListContent->setVisible(false);
  bkmkContent->addWidget(editListContent);

  auto toolbar = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_bookmark.svg"), "");
  Button* editListBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_draw.svg"), "Edit List");
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
  Button* sortBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_settings.svg"), "Sort");
  sortBtn->setMenu(sortMenu);

  Button* mapAreaBkmksBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_pin.svg"), "Bookmarks in map area only");
  mapAreaBkmksBtn->onClicked = [this](){
    mapAreaBkmks = !mapAreaBkmks;
    if(mapAreaBkmks)
      onMapChange();
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
      return true;
    }
    return false;
  });

  // handle visible bookmark lists
  //DB_exec(app->bkmkDB, "SELECT list_id FROM lists_state WHERE visible = 1;", [&](sqlite3_stmt* stmt){
  //  populateBkmks(sqlite3_column_int(stmt, 0), false);
  //});

  Menu* bkmkMenu = createMenu(Menu::VERT_LEFT);
  bkmkMenu->autoClose = true;
  bkmkMenu->addHandler([this, bkmkMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(bkmkMenu->selectFirst(".child-container"));

      // TODO: pinned bookmarks - timestamp column = INF?
      const char* query = "SELECT rowid, title, props, notes, lng, lat FROM bookmarks ORDER BY timestamp LIMIT 8;";
      DB_exec(app->bkmkDB, query, [&](sqlite3_stmt* stmt){
        std::string namestr = (const char*)(sqlite3_column_text(stmt, 1));
        std::string propstr = (const char*)(sqlite3_column_text(stmt, 2));
        double lng = sqlite3_column_double(stmt, 4);
        double lat = sqlite3_column_double(stmt, 5);
        //const char* notestr = (const char*)(sqlite3_column_text(stmt, 2));
        bkmkMenu->addItem(namestr.c_str(), SvgGui::useFile(":/icons/ic_menu_bookmark.svg"), [=](){
          app->setPickResult(LngLat(lng, lat), namestr, propstr); });
      });

    }
    return false;
  });

  Button* bkmkBtn = app->createPanelButton(SvgGui::useFile(":/icons/ic_menu_bookmark.svg"), "Places");
  bkmkBtn->setMenu(bkmkMenu);
  bkmkBtn->onClicked = [this](){
    populateLists(false);
  };

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
