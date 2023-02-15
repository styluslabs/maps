#include "bookmarks.h"
#include "mapsapp.h"
#include "util.h"
#include "resources.h"
#include "imgui.h"
#include "imgui_stl.h"

#include "sqlite3/sqlite3.h"
#include "rapidjson/document.h"

// bookmarks (saved places)

static sqlite3* bkmkDB = NULL;

void MapsBookmarks::hideBookmarks()
{
  for(MarkerID mrkid : bkmkMarkers)
    app->map->markerSetVisible(mrkid, false);
}

void MapsBookmarks::showGUI()
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
    DB_exec(bkmkDB, "CREATE TABLE IF NOT EXISTS bookmarks(osm_id TEXT, list TEXT, props TEXT, notes TEXT, lng REAL, lat REAL);");
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

  if (!std::isnan(app->pickResultCoord.latitude) && ImGui::Button("Save Current Place")) {
    rapidjson::Document doc;
    doc.Parse(app->pickResultProps.c_str());
    std::string osm_id;
    if(doc.IsObject() && doc.HasMember("osm_id") && doc.HasMember("osm_type"))
      osm_id = doc["osm_type"].GetString() + std::string(":") + doc["osm_id"].GetString();
    addBookmark(currList.c_str(), osm_id.c_str(), app->pickResultProps.c_str(), placeNotes.c_str(), app->pickResultCoord);
    updatePlaces = true;
  }
}

void MapsBookmarks::addBookmark(const char* list, const char* osm_id, const char* props, const char* note, LngLat lnglat, int rowid)
{
  const char* query = rowid >= 0 ?
      "UPDATE bookmarks SET osm_id = ?, list = ?, props = ?, notes = ?, lng = ?, lat = ? WHERE rowid = ?" :
      "INSERT INTO bookmarks (osm_id,list,props,notes,lng,lat) VALUES (?,?,?,?,?,?);";
  DB_exec(bkmkDB, query, NULL, [&](sqlite3_stmt* stmt){
    sqlite3_bind_text(stmt, 1, osm_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, list, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, props, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, note, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 5, lnglat.longitude);
    sqlite3_bind_double(stmt, 6, lnglat.latitude);
    if(rowid >= 0)
      sqlite3_bind_int(stmt, 7, rowid);
  });
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

// New GUI

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"

Widget* createListContainer()
{
  static const char* protoSVG = R"#(
    <g id="list-scroll-content" box-anchor="hfill" layout="flex" flex-direction="column" flex-wrap="nowrap" justify-content="flex-start">
    </g>
  )#";

  placeInfoProto.reset(loadSVGFragment(placeInfoProtoSVG));


}

void MapsBookmarks::populateLists()
{
  if(!listsPanel) {
    listsContent = createListContainer();
    auto headerTitle = createHeaderTitle(SvgGui::useFile(":/icons/ic_menu_drawer.svg"), "Bookmark Lists");
    auto listHeader = app->createPanelHeader(NULL, headerTitle, false);
    listsPanel = app->createMapPanel(listsContent, listHeader);
  }
  app->showPanel(listsPanel);
  app->gui->deleteContents(listsContent, ".listitem");

  DB_exec(bkmkDB, "SELECT list, COUNT(1) FROM bookmarks GROUP by list;", [this](sqlite3_stmt* stmt){
    std::string list = (const char*)(sqlite3_column_text(stmt, 0));
    int nplaces = sqlite3_column_int(stmt, 1);

    Button* item = new Button(bkmkListProto->clone());

    item->onClicked = [this, list](){
      populateBkmks(list);
    };

    Button* showBtn = new Button(item->containerNode()->selectFirst(".visibility-btn"));
    showBtn->onClicked = [](){
      // how to show bookmarks presistently?
    };

    SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
    titlenode->addText(list.c_str());

    SvgText* detailnode = static_cast<SvgText*>(item->containerNode()->selectFirst(".detail-text"));
    detailnode->addText(nplaces == 1 ? "1 place" : fstring("%d places", nplaces).c_str());

    listsContent->addWidget(item);
  });
}

void MapsBookmarks::populateBkmks(const std::string& listname)
{
  if(!bkmkPanel) {
    bkmkContent = createListContainer();
    auto headerTitle = createHeaderTitle(SvgGui::useFile(":/icons/ic_menu_bookmark.svg"), "");
    auto bkmkHeader = app->createPanelHeader([this](){ app->showPanel(listsPanel); }, headerTitle);
    bkmkPanel = app->createMapPanel(bkmkContent, bkmkHeader);
  }
  app->showPanel(bkmkPanel);
  app->gui->deleteContents(bkmkContent, ".listitem");

  bkmkPanel->selectFirst(".panel-title")->setText(listname.c_str());

  Map* map = app->map;
  size_t markerIdx = 0;
  const char* query = "SELECT rowid, props, notes, lng, lat FROM bookmarks WHERE list = ?;";
  DB_exec(bkmkDB, query, [&](sqlite3_stmt* stmt){
    double lng = sqlite3_column_double(stmt, 3);
    double lat = sqlite3_column_double(stmt, 4);
    //placeRowIds.push_back(sqlite3_column_int(stmt, 0));
    rapidjson::Document doc;
    doc.Parse((const char*)(sqlite3_column_text(stmt, 1)));

    const char* notestr = (const char*)(sqlite3_column_text(stmt, 2));


    if(markerIdx >= bkmkMarkers.size())
      bkmkMarkers.push_back(map->markerAdd());
    map->markerSetVisible(bkmkMarkers[markerIdx], true);
    // note that 6th decimal place of lat/lng is 11 cm (at equator)
    std::string namestr = doc.IsObject() && doc.HasMember("name") ?
        doc["name"].GetString() : fstring("%.6f, %.6f", lat, lng);

    map->markerSetStylingFromPath(bkmkMarkers[markerIdx], "layers.bookmark-marker.draw.marker");

    Properties mprops;
    mprops.set("priority", markerIdx);
    mprops.set("name", namestr);
    map->markerSetProperties(bkmkMarkers[markerIdx], std::move(mprops));

    map->markerSetPoint(bkmkMarkers[markerIdx], LngLat(lng, lat));
    ++markerIdx;


    Button* item = new Button(placeListProto->clone());

    item->onClicked = [this](){
      app->setPickResult(LngLat(lng, lat), namestr, );
    };

    SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
    titlenode->addText(namestr.c_str());

    SvgText* notenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".note-text"));
    notenode->addText(notestr);

    bkmkContent->addWidget(item);

  }, [&](sqlite3_stmt* stmt){
    sqlite3_bind_text(stmt, 1, listname.c_str(), -1, SQLITE_STATIC);
  });
  // hide unused markers
  for(; markerIdx < bkmkMarkers.size(); ++markerIdx)
    map->markerSetVisible(bkmkMarkers[markerIdx], false);
}

Widget* MapsBookmarks::getPlaceInfoSection(const std::string& osm_id, LngLat pos)
{
  Widget* widget = new Button(placeInfoSectionProto->clone());
  SvgText* listnode = static_cast<SvgText*>(widget->containerNode()->selectFirst(".list-name-text"));
  SvgText* notenode = static_cast<SvgText*>(widget->containerNode()->selectFirst(".note-text"));

  int rowid = -1;
  std::string liststr, notestr;
  // attempt lookup w/ osm_id if passed
  // - if no match, lookup by lat,lng but only accept hit w/o osm_id if osm_id is passed
  const char* query1 = "SELECT rowid, osm_id, list, props, notes, lng, lat FROM bookmarks WHERE osm_id = ?;";
  DB_exec(bkmkDB, query1, [&](sqlite3_stmt* stmt){
    rowid = sqlite3_column_int(stmt, 0);
    liststr = (const char*)(sqlite3_column_text(stmt, 2));
    notestr = (const char*)(sqlite3_column_text(stmt, 4));
  }, [&](sqlite3_stmt* stmt){
    sqlite3_bind_text(stmt, 1, osm_id.c_str(), -1, SQLITE_TRANSIENT);
  });

  if(liststr.empty()) {
    const char* query2 = "SELECT rowid, osm_id, list, props, notes, lng, lat FROM bookmarks WHERE "
        "lng >= ? AND lat >= ? AND lng <= ? AND lat <= ? LIMIT 1;";
    DB_exec(bkmkDB, query2, [&](sqlite3_stmt* stmt){
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

  listnode->addText(liststr.c_str());
  notenode->addText(notestr.c_str());

  Widget* infoStack = widget->selectFirst("bkmk-display-container");

  // bookmark editing

  auto editToolbar = createToolbar();
  auto acceptBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_accept.svg"));
  auto cancelBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_cancel.svg"));

  editToolbar->addWidget(new Widget(createTextNode("Edit Bookmark")));
  editToolbar->addWidget(createStretch());
  editToolbar->addWidget(acceptBtn);
  editToolbar->addWidget(cancelBtn);

  std::vector<std::string> lists;
  DB_exec(bkmkDB, "SELECT DISTINCT list FROM bookmarks;", [&](sqlite3_stmt* stmt){
    lists.emplace_back((const char*)(sqlite3_column_text(stmt, 0)));
  });
  auto listsCombo = createComboBox(lists);
  auto noteEdit = createTextEdit();
  noteEdit->setText(notestr.c_str());
  listsCombo->setText(liststr.c_str());

  auto editStack = createColumn();
  editStack->addWidget(editToolbar);
  editStack->addWidget(createTitledRow("List", listsCombo));
  editStack->addWidget(createTitledRow("Note", noteEdit));

  acceptBtn->onClicked = [&, rowid](){
    listnode->addText(listsCombo->text());
    notenode->addText(noteEdit->text().c_str());
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

Widget* MapsBookmarks::createUI()
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

        <g class="toolbutton visibility-btn" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/icons/ic_menu_pin.svg"/>
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

  Menu* bkmkMenu = createMenu(Menu::VERT_LEFT);
  bkmkMenu->autoClose = true;
  bkmkMenu->addHandler([this, bkmkMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(bkmkMenu->selectFirst(".child-container"));

      // TODO: show most recent bookmarks (add timestamp column to bookmark table?)

    }
    return false;
  });

  Button* bkmkBtn = createToolbutton(SvgGui::useFile(":/icons/ic_menu_bookmark.svg"), "Places");
  bkmkBtn->setMenu(bkmkMenu);
  bkmkBtn->onClicked = [this](){
    populateLists();
  };

  return bkmkBtn;
}
