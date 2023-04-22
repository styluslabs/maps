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



// how to create new list?
// - just enter name in text combo box when creating bookmark
// place info: what if place is member of multiple lists?  use list w/ checkboxes instead of combobox? ... how would we add to new list in this case?  listbox for existing lists + combo box for new one?
// ... list box w/ checkboxes for existing lists + text box for new one?  combobox which stays open w/ checkboxes?
// ... still use combo box if not member of any list?
// ... only show bookmark info if accessed through bookmark?

// instead of list box, just rows w/ delete button (to remove from existing lists) and combo box to add to new list

// combo box isn't the best fit for mobile UI ... could switch to modal scrolling list if number of items exceeds some threshold?



// combo box needs to display centered scrolling list on mobile

// Menu doesn't dim background ... so let's make separate SelectDialog class for now and decide if and how to combine w/ ComboBox later

// should SelectDialog include the button which opens it (in which case it is just a simple control - dialog stuff is hidden inside)? ... yes, I think so
// - for single select, control would look just like combobox ... how could we provide option to use a different style control?  Just different svg template?

// adding new item (equiv of text combo box) - "New item" item at top of list, becomes text edit + optional additional controls when tapped
// - new item control passed to create fn?  how to reset fields to defaults if reusing?  returned from callback invoked when new item clicked?

// data model for multi-select - bool vector member ... callback only called when dialog closed ... how does this work if new items added at top?


// textcombobox is not obvious enough for creating new list - we need to do "New list" item in list of lists

// I don't think we should add the extra step of having to click accept by using multi-select for the rare case of place in multiple bookmark lists


// Note we need single-select for map sources too!


SelectDialog::SelectDialog(std::string title, const std::vector<std::string>& _items) : Dialog(createDialogNode())
{
  Widget* dialogBody = selectFirst(".body-container");


  Widget* content = createColumn();
  addItems(_items);


  dialogBody->addWidget(new ScrollWidget(new SvgDocument(), content));
}

void SelectDialog::addItems(const std::vector<std::string>& _items)
{
  SvgNode* protonode = content->containerNode()->selectFirst(".item_proto");
  // populate items by cloning prototype
  for(const std::string& s : _items) {
    int ii = items.size();
    items.emplace_back(s);
    Button* btn = new Button(protonode->clone());
    btn->onClicked = [this, ii](){
      updateIndex(ii);
      finish();
    };
    btn->selectFirst("text")->setText(s.c_str());
    btn->setVisible(true);  // prototypes have display="none"
    content->addWidget(btn);
  }
}


ComboBox::ComboBox(SvgNode* n, const std::vector<std::string>& _items) : Widget(n), currIndex(0)
{
 comboMenu = new Menu(containerNode()->selectFirst(".combo_menu"), Menu::VERT_RIGHT);
  //Menu* combomenu = new Menu(widgetNode("#combo_menu"), Menu::VERT_RIGHT);
  //comboText = selectFirst(".combo_text");
  SvgNode* textNode = containerNode()->selectFirst(".combo_text");
  comboText = textNode->hasExt() ? static_cast<TextBox*>(textNode->ext()) : new TextBox(textNode);
  addItems(_items);

  setText(items.front().c_str());
  // combobox dropdown behaves the same as a menu
  SvgNode* btn = containerNode()->selectFirst(comboText->isEditable() ? ".combo_open" : ".combo_content");
  Button* comboopen = new Button(btn);
  comboopen->setMenu(comboMenu);
  // when combo menu opens, set min width to match the box
  comboopen->onPressed = [this](){
    SvgNode* minwidthnode = containerNode()->selectFirst(".combo-menu-min-width");
    if(minwidthnode && minwidthnode->type() == SvgNode::RECT) {
      SvgRect* rectNode = static_cast<SvgRect*>(minwidthnode);
      real sx = node->bounds().width()/rectNode->bounds().width();
      rectNode->setRect(Rect::wh(sx*rectNode->getRect().width(), rectNode->getRect().height()));
    }
  };

  // allow stepping through items w/ up/down arrow
  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_KEYDOWN && (event->key.keysym.sym == SDLK_UP || event->key.keysym.sym == SDLK_DOWN)) {
      updateIndex(currIndex + (event->key.keysym.sym == SDLK_UP ? -1 : 1));
      return true;
    }
    if(event->type == SDL_KEYDOWN || event->type == SDL_KEYUP || event->type == SDL_TEXTINPUT
        || event->type == SvgGui::FOCUS_GAINED || event->type == SvgGui::FOCUS_LOST)
      return comboText->sdlEvent(gui, event);
    return false;
  });
}






void MapsBookmarks::populateLists(bool archived)
{
  if(!listsPanel) {
    listsContent = createColumn(); //createListContainer();
    auto listHeader = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Bookmark Lists");
    listsPanel = app->createMapPanel(listHeader, listsContent);

    archivedContent = createColumn();
    auto archivedHeader = app->createPanelHeader(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Archived Bookmaks");
    archivedPanel = app->createMapPanel(archivedHeader, archivedContent);
  }
  app->showPanel(archived ? archivedPanel : listsPanel);
  Widget* content = archived ? archivedContent : listsContent;
  app->gui->deleteContents(content, ".listitem");


  listsPanel->onRestore = [](){
    if(listsDirty)
      populateLists(false);
  };

  archivedPanel->onRestore = [](){
    if(archiveDirty)
      populateLists(true);
  };

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
  if(createUI && !bkmkPanel) {
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
  }

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

  Widget* infoStack = widget->selectFirst("bkmk-display-container");
  // bookmark editing
  auto editToolbar = createToolbar();
  auto acceptBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_accept.svg"));
  auto cancelBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_cancel.svg"));
  auto removeBtn = createPushbutton("Remove");

  editToolbar->addWidget(new Widget(createTextNode("Edit Bookmark")));
  editToolbar->addWidget(createStretch());
  editToolbar->addWidget(acceptBtn);
  editToolbar->addWidget(cancelBtn);

  std::vector<std::string> lists;  // = {"None"};
  DB_exec(app->bkmkDB, "SELECT title FROM lists WHERE archived = 0;", [&](sqlite3_stmt* stmt){  //SELECT DISTINCT list FROM bookmarks;
    lists.emplace_back((const char*)(sqlite3_column_text(stmt, 0)));
  });
  auto listsCombo = createComboBox(lists);  // createTextComboBox not sufficiently obvious for creating new list
  auto noteEdit = createTextEdit();
  noteEdit->setText(notestr.c_str());
  listsCombo->setText(liststr.c_str());

  auto editStack = createColumn();
  editStack->addWidget(editToolbar);
  editStack->addWidget(createTitledRow("List", listsCombo, removeBtn));
  editStack->addWidget(createTitledRow("Note", noteEdit));

  acceptBtn->onClicked = [&, rowid](){
    std::string listname = listsCombo->text();
    rapidjson::Document& doc = app->pickResultProps;
    std::string title = doc.IsObject() && doc.HasMember("name") ?  doc["name"].GetString()
        : fstring("Pin: %.6f, %.6f", app->pickResultCoord.latitude, app->pickResultCoord.longitude);
    addBookmark(listsCombo->text(), osmIdFromProps(doc).c_str(), title.c_str(),
        rapidjsonToStr(doc).c_str(), noteEdit->text().c_str(), app->pickResultCoord, rowid);
    listText->setText(listsCombo->text());
    noteText->setText(noteEdit->text().c_str());
    // hide editing stack, show display stack
    editStack->setVisible(false);
    infoStack->setVisible(true);
  };

  cancelBtn->onClicked = [&](){
    editStack->setVisible(false);
    infoStack->setVisible(true);
  };

  removeBtn->onClicked = [&](){
    DB_exec(app->bkmkDB, "DELETE FROM bookmarks WHERE rowid = ?;", NULL, [&](sqlite3_stmt* stmt){
      sqlite3_bind_int(stmt, 1, rowid);
    });
    editStack->setVisible(false);  // note that we do not redisplay info stack
  };

  editStack->setVisible(false);
  widget->selectFirst("bkmk-edit-container")->addWidget(editStack);

  Button* bkmkBtn = new Button(widget->containerNode()->selectFirst("bkmk-icon-container"));
  bkmkBtn->onClicked = [&](){
    editStack->setVisible(true);
    infoStack->setVisible(false);
  };

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
    <g margin="0 5" layout="box" box-anchor="hfill">
      <g class="bkmk-edit-container" box-anchor="hfill" layout="box"></g>
      <g class="bkmk-display-container" layout="flex" flex-direction="row" box-anchor="left">
        <g class="bkmk-icon-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_bookmark.svg"/>
        </g>
        <g layout="box" box-anchor="vfill">
          <text class="list-name-text" box-anchor="left" margin="0 10"></text>
          <text class="note-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
        </g>
      </g>
    </g>
  )";
  placeInfoSectionProto.reset(loadSVGFragment(placeInfoSectionProtoSVG));

  // DB setup
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS lists(id INTEGER, title TEXT, notes TEXT, color TEXT, archived INTEGER DEFAULT 0);");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS lists_state(list_id INTEGER, order INTEGER, visible INTEGER);");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS bookmarks(osm_id TEXT, list_id INTEGER, title TEXT, props TEXT, notes TEXT, lng REAL, lat REAL, timestamp INTEGER DEFAULT CAST(strftime('%s') AS INTEGER));");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS saved_views(title TEXT UNIQUE, lng REAL, lat REAL, zoom REAL, rotation REAL, tilt REAL, width REAL, height REAL);");

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
