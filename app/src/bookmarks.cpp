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

// bookmarks (saved places)

void MapsBookmarks::addBookmark(const char* list, const char* osm_id, const char* name, const char* props, const char* note, LngLat pos, int rowid)
{
  // WITH list_id AS (SELECT rowid FROM lists where title = ?)

  const char* query = rowid >= 0 ?
      "UPDATE bookmarks SET osm_id = ?, list = ?, title = ?, props = ?, notes = ?, lng = ?, lat = ? WHERE rowid = ?" :  // list = (SELECT rowid FROM lists where title = ?)
      "INSERT INTO bookmarks (osm_id,list,props,notes,lng,lat) VALUES (?,?,?,?,?,?);";
  DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
    sqlite3_bind_text(stmt, 1, osm_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, list, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, props, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, note, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 6, pos.longitude);
    sqlite3_bind_double(stmt, 7, pos.latitude);
    if(rowid >= 0)
      sqlite3_bind_int(stmt, 8, rowid);
  });

  bkmkPanelDirty = true;
  if(rowid >= 0)
    return;

  auto it = bkmkMarkers.find(list);
  if(it != bkmkMarkers.end()) {
    std::string propstr(props);
    std::string namestr(name);
    auto onPicked = [=](){ app->setPickResult(pos, namestr, propstr); };
    it->second->createMarker(pos, onPicked, {{{"name", name}}});
  }
}

/*Widget* createListContainer()
{
  static const char* protoSVG = R"#(
    <g id="list-scroll-content" box-anchor="hfill" layout="flex" flex-direction="column" flex-wrap="nowrap" justify-content="flex-start">
    </g>
  )#";

  //placeInfoProto.reset(loadSVGFragment(placeInfoProtoSVG));
}*/



// use list_id instead of name for bkmkMarkers map

// "+" button next to list combo box in place info section to add place to another bookmark list (shows the create bookmark section)

// how to load markers.yaml for every map ... list of yaml files to add to every scene in config file

// "Widget* addWidgets(std::vector<Widget*> widgets) { for(Widget* w : widgets) addWidget(w); return this; }

// how to dedup drag and drop reordering of tracks, sources, bookmark lists?
// - reorderableList widget ... not necessary
// - createReorderingBtn() or createReorderingFn() should be sufficient

// show date for bookmark: with notes and in list

// NEXT: archiving tracks


void MapsBookmarks::chooseBookmarkList(std::function<void(std::string)> callback)  //int rowid)
{
  Dialog* dialog = new Dialog( setupWindowNode(chooseListProto->clone()) );
  Widget* content = createColumn();

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
    dialog->finish(Dialog::ACCEPTED);
    callback(list);
  };
  newListContent->addWidget(createTitledRow("Title", newListTitle));
  newListContent->addWidget(createTitledRow("Color", newListColor));
  newListContent->addWidget(createListBtn);
  newListContent->setVisible(false);
  content->addWidget(newListContent);

  Button* newListBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_add_folder.svg"), "Create List");
  newListBtn->onClicked = [=](){
    newListContent->setVisible(!newListContent->isVisible());
  };

  Button* cancelBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_back.svg"), "Cancel");
  cancelBtn->onClicked = [=](){
    dialog->finish(Dialog::CANCELLED);
  };

  const char* query = "SELECT title FROM lists JOIN lists_state AS s ON lists.rowid = s.list_id WHERE lists.archived = 0 ORDER BY s.order;";
  DB_exec(app->bkmkDB, query, [=](sqlite3_stmt* stmt){
    std::string list = (const char*)(sqlite3_column_text(stmt, 0));
    Button* item = new Button(listSelectProto->clone());
    item->onClicked = [=](){
      dialog->finish(Dialog::ACCEPTED);
      callback(list);
    };
    SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
    titlenode->addText(list.c_str());
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
  dialogBody->addWidget(new ScrollWidget(new SvgDocument(), content));

  app->gui->showModal(dialog, app->gui->windows.front()->modalOrSelf());
}

void MapsBookmarks::populateLists(bool archived)
{
  app->showPanel(archived ? archivedPanel : listsPanel);
  Widget* content = archived ? archivedContent : listsContent;
  app->gui->deleteContents(content, ".listitem");

  if(archived) archiveDirty = false; else listsDirty = false;

  const char* query = "SELECT title, COUNT(1) FROM lists JOIN bookmarks AS b ON lists.rowid = b.list_id "
      "JOIN lists_state AS s ON lists.rowid = s.list_id WHERE lists.archived = ? GROUP by lists.row_id ORDER BY s.order;";
  DB_exec(app->bkmkDB, query, [=](sqlite3_stmt* stmt){  //"SELECT list, COUNT(1) FROM bookmarks GROUP by list;"
    std::string list = (const char*)(sqlite3_column_text(stmt, 0));
    int nplaces = sqlite3_column_int(stmt, 1);

    Button* item = new Button(bkmkListProto->clone());

    item->onClicked = [this, list](){
      populateBkmks(list, true);
    };

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

          const char* q = "UPDATE lists_state SET order = (CASE order WHEN ?1 THEN ?2 WHEN ?2 THEN ?1) WHERE order in (?1, ?2);";
          DB_exec(app->bkmkDB, q, NULL, [&](sqlite3_stmt* stmt1){
            sqlite3_bind_int(stmt1, 1, ii);
            sqlite3_bind_int(stmt1, 1, dy < 0 ? ii-1 : ii+1);
          });

          SvgNode* next = *it;
          parent->removeChild(item->node);
          parent->addChild(item->node, next);
        }
        return true;
      }
      return false;
    });

    Button* showBtn = new Button(item->containerNode()->selectFirst(".visibility-btn"));
    auto it = bkmkMarkers.find(list);
    if(it != bkmkMarkers.end())
      showBtn->setChecked(it->second->visible);
    showBtn->onClicked = [this, list, showBtn](){
      bool visible = !showBtn->isChecked();
      showBtn->setChecked(visible);
      std::string q1 = fstring("UPDATE lists_state SET visible = %d WHERE list_id = ?;", visible ? 1 : 0);
      DB_exec(app->bkmkDB, q1.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });

      auto it = bkmkMarkers.find(list);
      if(it == bkmkMarkers.end()) {
        populateBkmks(list, false);
      }
      else
        it->second->setVisible(visible);  //!it->second->visible);
    };

    Button* overflowBtn = new Button(item->containerNode()->selectFirst(".overflow-btn"));
    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowMenu->addItem(archived ? "Unarchive" : "Archive", [=](){

      std::string q1 = fstring("UPDATE lists_state SET order = (SELECT COUNT(1) FROM lists WHERE archived = %d) WHERE list_id = ?;", archived ? 0 : 1);
      DB_exec(app->bkmkDB, q1.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });

      std::string q2 = fstring("UPDATE lists SET archived = %d WHERE rowid = ?;", archived ? 0 : 1);
      DB_exec(app->bkmkDB, q2.c_str(), NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
      app->gui->deleteWidget(item);
      if(archived) listsDirty = true; else archiveDirty = true;
    });

    // undo?  maybe try https://www.sqlite.org/undoredo.html (using triggers to populate an undo table)
    overflowMenu->addItem("Delete", [=](){
      const char* q = "DELETE FROM bookmarks WHERE list_id = ?";
      DB_exec(app->bkmkDB, q, NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });

      const char* q1 = "DELETE FROM lists WHERE rowid = ?";
      DB_exec(app->bkmkDB, q1, NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });

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

// Note each MapPanel is attached to UI tree (but invisible unless in use) and thus deleted when UI is deleted
class MapPanel : public Widget
{
public:
  std::function<void()> onRestore;
  std::function<void()> onClose;
};

void MapsBookmarks::hideBookmarks(const std::string& excludelist)
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

void MapsBookmarks::populateBkmks(const std::string& listname, bool createUI)
{
  if(createUI) {
    app->showPanel(bkmkPanel);
    app->gui->deleteContents(bkmkContent, ".listitem");
    bkmkPanel->selectFirst(".panel-title")->setText(listname.c_str());

    bkmkPanel->onRestore = [this, listname](){
      if(bkmkPanelDirty)
        populateBkmks(listname, true);
    };

    if(hiddenGroups.empty()) {
      hideBookmarks(listname);
      bkmkPanel->onClose = [this](){ restoreBookmarks(); };
    }

    bkmkPanelDirty = false;
  }

  MarkerGroup* markerGroup = NULL;
  auto it = bkmkMarkers.find(listname);
  if(it == bkmkMarkers.end())
    markerGroup = *bkmkMarkers.emplace(listname, "layers.bookmark-marker.draw.marker").first;

  std::string srt = app->config["bookmarks"]["sort"].as<std::string>("date");
  std::string strStr = srt == "name" ? "title" : srt == "dist" ? "osmSearchRank(-1, lng, lat)" : "timestamp DESC";
  std::string query = "SELECT rowid, title, props, notes, lng, lat FROM bookmarks WHERE list = ? ORDER BY " + strStr + ";";
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
    sqlite3_bind_text(stmt, 1, listname.c_str(), -1, SQLITE_STATIC);
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
  const char* query1 = "SELECT rowid, osm_id, list, props, notes, lng, lat FROM bookmarks WHERE osm_id = ?;";
  DB_exec(app->bkmkDB, query1, [&](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    const char* liststr = (const char*)(sqlite3_column_text(stmt, 2));
    const char* notestr = (const char*)(sqlite3_column_text(stmt, 4));
    content->addWidget(getPlaceInfoSubSection(rowid, liststr, notestr));
  }, [&](sqlite3_stmt* stmt){
    sqlite3_bind_text(stmt, 1, osm_id.c_str(), -1, SQLITE_TRANSIENT);
  });

  if(content->containerNode()->children().empty()) {
    const char* query2 = "SELECT rowid, osm_id, list, props, notes, lng, lat FROM bookmarks WHERE "
        "lng >= ? AND lat >= ? AND lng <= ? AND lat <= ? LIMIT 1;";
    DB_exec(app->bkmkDB, query2, [&](sqlite3_stmt* stmt){
      if(osm_id.empty() || sqlite3_column_text(stmt, 1)[0] == '\0') {
        int rowid = sqlite3_column_int(stmt, 0);
        const char* liststr = (const char*)(sqlite3_column_text(stmt, 2));
        const char* notestr = (const char*)(sqlite3_column_text(stmt, 4));
        content->addWidget(getPlaceInfoSubSection(rowid, liststr, notestr));
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
    content->addWidget(getPlaceInfoSubSection(-1, "", ""));  // for adding to bookmark list

  return content;
}

Widget* MapsBookmarks::getPlaceInfoSubSection(int rowid, std::string liststr, std::string notestr)
{
  Widget* widget = new Widget(placeInfoSectionProto->clone());
  TextBox* listText = new TextBox(static_cast<SvgText*>(widget->containerNode()->selectFirst(".list-name-text")));
  TextBox* noteText = new TextBox(static_cast<SvgText*>(widget->containerNode()->selectFirst(".note-text")));
  listText->setText(liststr.c_str());
  noteText->setText(notestr.c_str());

  Widget* bkmkStack = widget->selectFirst("bkmk-display-container");
  Button* createBkmkBtn = new Button(widget->containerNode()->selectFirst(".addbkmk-btn"));
  // bookmark editing
  auto editToolbar = createToolbar();
  auto noteEdit = createTextEdit();
  auto acceptNoteBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_accept.svg"));
  auto cancelNoteBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_cancel.svg"));

  editToolbar->addWidget(noteEdit);
  editToolbar->addWidget(acceptNoteBtn);
  editToolbar->addWidget(cancelNoteBtn);
  editToolbar->setVisible(false);
  widget->selectFirst("bkmk-edit-container")->addWidget(editToolbar);

  acceptNoteBtn->onClicked = [=](){
    DB_exec(app->bkmkDB, "UPDATE bookmarks SET notes = ? WHERE rowid = ?", NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, noteEdit->text().c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(stmt, 2, rowid);
    });
    noteText->setText(noteEdit->text().c_str());
    editToolbar->setVisible(false);
    noteText->setVisible(true);
  };

  cancelNoteBtn->onClicked = [&](){
    editToolbar->setVisible(false);
    noteText->setVisible(true);
  };

  auto removeBtn = new Button(widget->containerNode()->selectFirst(".discard-btn"));
  removeBtn->onClicked = [=](){
    DB_exec(app->bkmkDB, "DELETE FROM bookmarks WHERE rowid = ?;", NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_int(stmt, 1, rowid);
    });
    editToolbar->setVisible(false);  // note that we do not redisplay info stack
    noteText->setVisible(false);
    createBkmkBtn->setVisible(true);
    bkmkStack->setVisible(false);
  };

  auto addNoteBtn = new Button(widget->containerNode()->selectFirst(".addnote-btn"));
  addNoteBtn->setVisible(notestr.empty());
  addNoteBtn->onClicked = [=](){
    editToolbar->setVisible(true);
    app->gui->setFocused(noteEdit);
  };

  auto setListFn = [=](std::string list){
    DB_exec(app->bkmkDB, "UPDATE bookmarks SET list = ? WHERE rowid = ?", NULL, [=](sqlite3_stmt* stmt1){
      sqlite3_bind_text(stmt1, 1, list.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(stmt1, 2, rowid);
    });
    listText->setText(list.c_str());
  };

  Button* chooseListBtn = new Button(widget->containerNode()->selectFirst(".combobox"));
  chooseListBtn->onClicked = [=](){
    chooseBookmarkList(setListFn);  //rowid);
  };

  auto createBkmkFn = [&, rowid](std::string list){
    rapidjson::Document& doc = app->pickResultProps;
    std::string title = doc.IsObject() && doc.HasMember("name") ?  doc["name"].GetString()
        : fstring("Pin: %.6f, %.6f", app->pickResultCoord.latitude, app->pickResultCoord.longitude);
    addBookmark(list.c_str(), osmIdFromProps(doc).c_str(), title.c_str(),
        rapidjsonToStr(doc).c_str(), "", app->pickResultCoord, rowid);
    listText->setText(list.c_str());
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
        <text>Add Bookmark<text/>
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
          <text>Add Note<text/>
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
        <g class="title-container" box-anchor="fill" layout="box"></g>
        <rect class="hrule title" box-anchor="hfill" width="20" height="2"/>
        <g class="body-container" box-anchor="fill" layout="flex" flex-direction="column"></g>
      </g>
    </svg>
  )";
  chooseListProto.reset(static_cast<SvgDocument*>(loadSVGFragment(chooseListProtoSVG)));

  // DB setup
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS lists(id INTEGER, title TEXT, notes TEXT, color TEXT, archived INTEGER DEFAULT 0);");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS lists_state(list_id INTEGER, order INTEGER, visible INTEGER);");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS bookmarks(osm_id TEXT, list_id INTEGER, title TEXT, props TEXT, notes TEXT, lng REAL, lat REAL, timestamp INTEGER DEFAULT CAST(strftime('%s') AS INTEGER));");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS saved_views(title TEXT UNIQUE, lng REAL, lat REAL, zoom REAL, rotation REAL, tilt REAL, width REAL, height REAL);");

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

  listsPanel->onRestore = [](){
    if(listsDirty)
      populateLists(false);
  };

  archivedPanel->onRestore = [](){
    if(archiveDirty)
      populateLists(true);
  };

  // Bookmarks panel
  bkmkContent = createColumn(); //createListContainer();
  Widget* editListContent = createColumn();
  TextEdit* listTitle = createTextEdit();
  TextEdit* listColor = createTextEdit();  // obviously needs to be replaced with a color picker
  Button* saveListBtn = createPushbutton("Apply");
  saveListBtn->onClicked = [=](){
    const char* query = "UPDATE lists SET title = ?, color = ? WHERE rowid = ?";
    DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, listTitle->text().c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, listColor->text().c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(stmt, 3, rowid);
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
    populateBkmks(list_name, true);  // class member to hold current list name or id?
  }, initSortIdx);
  Button* sortBtn = createToolbutton(SvgGui::useFile("icons/ic_menu_settings.svg"), "Sort");
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

  // handle visible bookmark lists
  DB_exec(app->bkmkDB, "SELECT list_id FROM lists_state WHERE visible = 1;", [&](sqlite3_stmt* stmt){
    populateBkmks(sqlite3_column_int(stmt, 0), false);
  });

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

  Button* bkmkBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_bookmark.svg"), "Places");
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
