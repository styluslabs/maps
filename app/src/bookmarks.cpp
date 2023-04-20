#include "bookmarks.h"
#include "mapsapp.h"
#include "util.h"
#include "resources.h"
//#include "imgui.h"
//#include "imgui_stl.h"

#include "sqlite3/sqlite3.h"
#include "rapidjson/document.h"

// bookmarks (saved places)

/*void MapsBookmarks::showGUI()
{
  if(!bkmkDB) {
    std::string dbPath = MapsApp::baseDir + "places.sqlite";
    if(sqlite3_open_v2(dbPath.c_str(), &bkmkDB, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
      logMsg("Error creating %s", dbPath.c_str());
      sqlite3_close(bkmkDB);
      bkmkDB = NULL;
      return;
    }
    //DB_exec(bkmkDB, "CREATE TABLE IF NOT EXISTS history(query TEXT UNIQUE);");
    DB_exec(bkmkDB, "CREATE TABLE IF NOT EXISTS bookmarks(osm_id TEXT, list TEXT, props TEXT, notes TEXT, lng REAL, lat REAL, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);");
    DB_exec(bkmkDB, "CREATE TABLE IF NOT EXISTS saved_views(title TEXT UNIQUE, lng REAL, lat REAL, zoom REAL, rotation REAL, tilt REAL, width REAL, height REAL);");
  }

  showPlacesGUI();
  showViewsGUI();
}

void MapsBookmarks::showPlacesGUI()
{
  static std::vector<int> placeRowIds;
  static std::vector<std::string> placeNames;
  static std::string placeNotes;
  static std::string currList;
  static std::string newListTitle;
  static int currListIdx = 0;
  static int currPlaceIdx = -1;
  static bool updatePlaces = true;

  if (!ImGui::CollapsingHeader("Saved Places", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  Map* map = app->map;
  std::vector<std::string> lists;
  DB_exec(bkmkDB, "SELECT DISTINCT list FROM bookmarks;", [&](sqlite3_stmt* stmt){
    lists.emplace_back((const char*)(sqlite3_column_text(stmt, 0)));
  });

  bool ent = ImGui::InputText("List Title", &newListTitle, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if (ImGui::Button("Create") || ent) {
    currList = newListTitle;
    currListIdx = -1;
    updatePlaces = true;
  }

  if(!lists.empty()) {
    std::vector<const char*> clists;
    for(const auto& s : lists)
      clists.push_back(s.c_str());

    if(ImGui::Combo("List", &currListIdx, clists.data(), clists.size())) {
      currList = lists[currListIdx];
      newListTitle.clear();
      updatePlaces = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
      DB_exec(bkmkDB, fstring("DELETE FROM bookmarks WHERE list = %s;", currList.c_str()).c_str());
    }
  }
  else if(currList.empty())
    return;

  if(updatePlaces) currPlaceIdx = -1;
  // TODO: dedup w/ search
  if(app->searchActive)
    updatePlaces = true;
  else if(updatePlaces) {
    placeNames.clear();
    placeRowIds.clear();
    size_t markerIdx = 0;
    const char* query = "SELECT rowid, props, lng, lat FROM bookmarks WHERE list = ?;";
    DB_exec(bkmkDB, query, [&](sqlite3_stmt* stmt){
      //if(bkmkMarkers.empty()) {
      //  std::string svg = fstring(markerSVG, "#12B5CB");
      //  app->textureFromSVG("bkmk-marker-cyan", (char*)svg.data(), 1.25f);
      //}
      double lng = sqlite3_column_double(stmt, 2);
      double lat = sqlite3_column_double(stmt, 3);
      placeRowIds.push_back(sqlite3_column_int(stmt, 0));
      rapidjson::Document doc;
      doc.Parse((const char*)(sqlite3_column_text(stmt, 1)));
      if(markerIdx >= bkmkMarkers.size())
        bkmkMarkers.push_back(map->markerAdd());
      map->markerSetVisible(bkmkMarkers[markerIdx], true);
      // note that 6th decimal place of lat/lng is 11 cm (at equator)
      std::string namestr = doc.IsObject() && doc.HasMember("name") ?
            doc["name"].GetString() : fstring("%.6f, %.6f", lat, lng);
      placeNames.push_back(namestr);
      //map->markerSetStylingFromString(bkmkMarkers[markerIdx], fstring(searchMarkerStyleStr, "bkmk-marker-cyan").c_str());
      map->markerSetStylingFromPath(bkmkMarkers[markerIdx], "layers.bookmark-marker.draw.marker");

      Properties mprops;
      mprops.set("priority", markerIdx);
      mprops.set("name", namestr);
      map->markerSetProperties(bkmkMarkers[markerIdx], std::move(mprops));

      map->markerSetPoint(bkmkMarkers[markerIdx], LngLat(lng, lat));
      ++markerIdx;
    }, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, currList.c_str(), -1, SQLITE_STATIC);
    });
    for(; markerIdx < bkmkMarkers.size(); ++markerIdx)
      map->markerSetVisible(bkmkMarkers[markerIdx], false);
    updatePlaces = false;
  }

  int prevPlaceIdx = currPlaceIdx;
  if(app->pickedMarkerId > 0) {
    for(size_t ii = 0; ii < bkmkMarkers.size(); ++ii) {
      if(bkmkMarkers[ii] == app->pickedMarkerId) {
        currPlaceIdx = ii;
        break;
      }
    }
  }

  std::vector<const char*> cnames;
  for(const auto& s : placeNames)
    cnames.push_back(s.c_str());

  if(ImGui::ListBox("Places", &currPlaceIdx, cnames.data(), cnames.size()) || app->pickedMarkerId > 0) {
    std::string query = fstring("SELECT props, notes, lng, lat FROM bookmarks WHERE rowid = %d;", placeRowIds[currPlaceIdx]);
    DB_exec(bkmkDB, query.c_str(), [&](sqlite3_stmt* stmt){
      double lng = sqlite3_column_double(stmt, 2);
      double lat = sqlite3_column_double(stmt, 3);
      placeNotes = (const char*)(sqlite3_column_text(stmt, 1));
      app->setPickResult(LngLat(lng, lat), placeNames[currPlaceIdx], (const char*)(sqlite3_column_text(stmt, 0)));
    });

    // What if pick result is cleared some other way?  How to restore bookmark marker?
    //app->onPickResultChanged = [this](){ updatePlaces = true; };
    //map->markerSetVisible(bkmkMarkers[currPlaceIdx], false);
    //if(prevPlaceIdx >= 0)
    //  map->markerSetVisible(bkmkMarkers[prevPlaceIdx], true);
    app->pickedMarkerId = 0;
  }

  if (ImGui::Button("Delete Place")) {
    DB_exec(bkmkDB, fstring("DELETE FROM bookmarks WHERE rowid = %d;", placeRowIds[currPlaceIdx]).c_str());
    updatePlaces = true;
  }

  ImGui::InputText("Notes", &placeNotes, ImGuiInputTextFlags_EnterReturnsTrue);

#if 0
  if (!std::isnan(app->pickResultCoord.latitude) && ImGui::Button("Save Current Place")) {
    rapidjson::Document doc;
    doc.Parse(app->pickResultProps.c_str());
    std::string osm_id;
    if(doc.IsObject() && doc.HasMember("osm_id") && doc.HasMember("osm_type"))
      osm_id = doc["osm_type"].GetString() + std::string(":") + doc["osm_id"].GetString();
    addBookmark(currList.c_str(), osm_id.c_str(), app->pickResultProps.c_str(), placeNotes.c_str(), app->pickResultCoord);
    updatePlaces = true;
  }
#endif
}

void MapsBookmarks::showViewsGUI()
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

void MapsBookmarks::addBookmark(const char* list, const char* osm_id, const char* props, const char* note, LngLat pos, int rowid)
{

  // WITH list_id AS (SELECT rowid FROM lists where title = ?)

  const char* query = rowid >= 0 ?
      "UPDATE bookmarks SET osm_id = ?, list = ?, props = ?, notes = ?, lng = ?, lat = ? WHERE rowid = ?" :  // list = (SELECT rowid FROM lists where title = ?)
      "INSERT INTO bookmarks (osm_id,list,props,notes,lng,lat) VALUES (?,?,?,?,?,?);";
  DB_exec(app->bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
    sqlite3_bind_text(stmt, 1, osm_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, list, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, props, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, note, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 5, pos.longitude);
    sqlite3_bind_double(stmt, 6, pos.latitude);
    if(rowid >= 0)
      sqlite3_bind_int(stmt, 7, rowid);
  });

  bkmkPanelDirty = true;
  if(rowid >= 0)
    return;

  auto it = bkmkMarkers.find(list);
  if(it != bkmkMarkers.end()) {
    rapidjson::Document doc;
    doc.Parse(props);

    std::string namestr = doc.IsObject() && doc.HasMember("name") ?
        doc["name"].GetString() : fstring("%.6f, %.6f", pos.latitude, pos.longitude);
    Properties mprops;
    //mprops.set("priority", markerIdx);
    mprops.set("name", namestr);
    std::string propstr(props);

    auto onPicked = [=](){ app->setPickResult(pos, namestr, propstr); };
    it->second->createMarker(pos, onPicked, std::move(mprops));
  }
}

// New GUI

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"

/*Widget* createListContainer()
{
  static const char* protoSVG = R"#(
    <g id="list-scroll-content" box-anchor="hfill" layout="flex" flex-direction="column" flex-wrap="nowrap" justify-content="flex-start">
    </g>
  )#";

  //placeInfoProto.reset(loadSVGFragment(placeInfoProtoSVG));
}*/

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


  const char* query = "SELECT title, COUNT(1) FROM lists JOIN bookmarks ON lists.rowid = bookmarks.list_id WHERE lists.archived = ? GROUP by lists.row_id;";
  DB_exec(app->bkmkDB, query, [=](sqlite3_stmt* stmt){  //"SELECT list, COUNT(1) FROM bookmarks GROUP by list;"
    std::string list = (const char*)(sqlite3_column_text(stmt, 0));
    int nplaces = sqlite3_column_int(stmt, 1);

    Button* item = new Button(bkmkListProto->clone());

    item->onClicked = [this, list](){
      populateBkmks(list, true);
    };

    Button* showBtn = new Button(item->containerNode()->selectFirst(".visibility-btn"));
    showBtn->onClicked = [this, list](){
      auto it = bkmkMarkers.find(list);
      if(it == bkmkMarkers.end()) {
        populateBkmks(list, false);
      }
      else
        it->second->setVisible(!it->second->visible);

      // how to show bookmarks presistently?
      // - where to store this flag?  DB? prefs? ... prefs seem more appropriate; we can just use yaml or json doc for prefs
      // - different colors for bookmarks? color is currently hardcoded in SVG

      // how to create new list?
      // - just enter name in text combo box when creating bookmark

      // - we need way to rename list; can also use this to set color
      // ... so we probably should use separate DB table w/ list props
      // ... and have dialog to edit list name, color, etc.

      // how to apply color to SVG markers?


      // NEED option to archive bookmark lists (tracks too?)

      // use list_id instead of name for bkmkMarkers map

      // place info: what if place is member of multiple lists?  use list w/ checkboxes instead of combobox?

      // need options for sorting list: distance, date, alphabetical



    };

    Button* overflowBtn = new Button(item->containerNode()->selectFirst(".overflow-btn"));

    Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
    overflowMenu->addItem(archived ? "Unarchive" : "Archive", [=](){
      const char* q = archived ? "UPDATE lists SET archived = 0 WHERE rowid = ?" : "UPDATE lists SET archived = 1 WHERE rowid = ?";
      DB_exec(app->bkmkDB, q, NULL, [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, rowid); });
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

  //Map* map = app->map;
  //size_t markerIdx = 0;
  const char* query = "SELECT rowid, props, notes, lng, lat FROM bookmarks WHERE list = ?;";
  DB_exec(app->bkmkDB, query, [&](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    std::string propstr = (const char*)(sqlite3_column_text(stmt, 1));
    const char* notestr = (const char*)(sqlite3_column_text(stmt, 2));
    double lng = sqlite3_column_double(stmt, 3);
    double lat = sqlite3_column_double(stmt, 4);
    rapidjson::Document doc;
    doc.Parse(propstr.c_str());

    std::string namestr = doc.IsObject() && doc.HasMember("name") ?
        doc["name"].GetString() : fstring("%.6f, %.6f", lat, lng);
    Properties mprops;
    //mprops.set("priority", markerIdx);
    mprops.set("name", namestr);

    auto onPicked = [=](){ app->setPickResult(LngLat(lng, lat), namestr, propstr); };
    if(markerGroup)
      markerGroup->createMarker(LngLat(lng, lat), onPicked, std::move(mprops));

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
  Widget* widget = new Widget(placeInfoSectionProto->clone());
  TextBox* listText = new TextBox(static_cast<SvgText*>(widget->containerNode()->selectFirst(".list-name-text")));
  TextBox* noteText = new TextBox(static_cast<SvgText*>(widget->containerNode()->selectFirst(".note-text")));

  int rowid = -1;
  std::string liststr, notestr;
  // attempt lookup w/ osm_id if passed
  // - if no match, lookup by lat,lng but only accept hit w/o osm_id if osm_id is passed
  const char* query1 = "SELECT rowid, osm_id, list, props, notes, lng, lat FROM bookmarks WHERE osm_id = ?;";
  DB_exec(app->bkmkDB, query1, [&](sqlite3_stmt* stmt){
    rowid = sqlite3_column_int(stmt, 0);
    liststr = (const char*)(sqlite3_column_text(stmt, 2));
    notestr = (const char*)(sqlite3_column_text(stmt, 4));
  }, [&](sqlite3_stmt* stmt){
    sqlite3_bind_text(stmt, 1, osm_id.c_str(), -1, SQLITE_TRANSIENT);
  });

  if(liststr.empty()) {
    const char* query2 = "SELECT rowid, osm_id, list, props, notes, lng, lat FROM bookmarks WHERE "
        "lng >= ? AND lat >= ? AND lng <= ? AND lat <= ? LIMIT 1;";
    DB_exec(app->bkmkDB, query2, [&](sqlite3_stmt* stmt){
      if(osm_id.empty() || sqlite3_column_text(stmt, 1)[0] == '\0') {
        rowid = sqlite3_column_int(stmt, 0);
        liststr = (const char*)(sqlite3_column_text(stmt, 2));
        notestr = (const char*)(sqlite3_column_text(stmt, 4));
      }
    }, [&](sqlite3_stmt* stmt){
      constexpr double delta = 0.00001;
      sqlite3_bind_double(stmt, 2, pos.longitude - delta);
      sqlite3_bind_double(stmt, 3, pos.latitude - delta);
      sqlite3_bind_double(stmt, 4, pos.longitude + delta);
      sqlite3_bind_double(stmt, 5, pos.latitude + delta);
    });
  }

  listText->setText(liststr.c_str());
  noteText->setText(notestr.c_str());

  Widget* infoStack = widget->selectFirst("bkmk-display-container");

  // bookmark editing

  auto editToolbar = createToolbar();
  auto acceptBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_accept.svg"));
  auto cancelBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_cancel.svg"));

  editToolbar->addWidget(new Widget(createTextNode("Edit Bookmark")));
  editToolbar->addWidget(createStretch());
  editToolbar->addWidget(acceptBtn);
  editToolbar->addWidget(cancelBtn);

  std::vector<std::string> lists = {"None"};
  DB_exec(app->bkmkDB, "SELECT title FROM lists WHERE archived = 0;", [&](sqlite3_stmt* stmt){  //SELECT DISTINCT list FROM bookmarks;
    lists.emplace_back((const char*)(sqlite3_column_text(stmt, 0)));
  });
  auto listsCombo = createTextComboBox(lists);
  auto noteEdit = createTextEdit();
  noteEdit->setText(notestr.c_str());
  listsCombo->setText(liststr.c_str());

  auto editStack = createColumn();
  editStack->addWidget(editToolbar);
  editStack->addWidget(createTitledRow("List", listsCombo));
  editStack->addWidget(createTitledRow("Note", noteEdit));

  acceptBtn->onClicked = [&, rowid](){
    std::string listname = listsCombo->text();
    if(listname == "None") {

    }


    listText->setText(listsCombo->text());
    noteText->setText(noteEdit->text().c_str());
    addBookmark(listsCombo->text(), osmIdFromProps(app->pickResultProps).c_str(),
        rapidjsonToStr(app->pickResultProps).c_str(), noteEdit->text().c_str(), app->pickResultCoord, rowid);
    // hide editing stack, show display stack
    editStack->setVisible(false);
    infoStack->setVisible(true);
  };

  cancelBtn->onClicked = [&](){
    editStack->setVisible(false);
    infoStack->setVisible(true);
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
        <g class="image-container" margin="2 5">
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
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS bookmarks(osm_id TEXT, list_id INTEGER, props TEXT, notes TEXT, lng REAL, lat REAL, timestamp INTEGER DEFAULT CAST(strftime('%s') AS INTEGER));");
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS saved_views(title TEXT UNIQUE, lng REAL, lat REAL, zoom REAL, rotation REAL, tilt REAL, width REAL, height REAL);");

  Menu* bkmkMenu = createMenu(Menu::VERT_LEFT);
  bkmkMenu->autoClose = true;
  bkmkMenu->addHandler([this, bkmkMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(bkmkMenu->selectFirst(".child-container"));

      // TODO: pinned bookmarks - timestamp column = INF?
      const char* query = "SELECT rowid, props, notes, lng, lat FROM bookmarks ORDER BY timestamp LIMIT 8;";
      DB_exec(app->bkmkDB, query, [&](sqlite3_stmt* stmt){
        std::string propstr = (const char*)(sqlite3_column_text(stmt, 1));
        double lng = sqlite3_column_double(stmt, 3);
        double lat = sqlite3_column_double(stmt, 4);
        //const char* notestr = (const char*)(sqlite3_column_text(stmt, 2));
        rapidjson::Document doc;
        doc.Parse(propstr.c_str());
        std::string namestr = doc.IsObject() && doc.HasMember("name") ?
            doc["name"].GetString() : fstring("%.6f, %.6f", lat, lng);
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
