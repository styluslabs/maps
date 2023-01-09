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

void MapsBookmarks::addBookmark(const char* list, const char* osm_id, const char* props, const char* note, LngLat lnglat)
{
  const char* strStmt = "INSERT INTO bookmarks (osm_id,list,props,notes,lng,lat) VALUES (?,?,?,?,?,?);";
  sqlite3_stmt* stmt;
  if(sqlite3_prepare_v2(bkmkDB, strStmt, -1, &stmt, NULL) != SQLITE_OK) {
    logMsg("sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(bkmkDB));
    return;
  }
  sqlite3_bind_text(stmt, 1, osm_id, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, list, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, props, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, note, -1, SQLITE_STATIC);
  sqlite3_bind_double(stmt, 5, lnglat.longitude);
  sqlite3_bind_double(stmt, 6, lnglat.latitude);
  if (sqlite3_step(stmt) != SQLITE_DONE)
    logMsg("sqlite3_step failed: %d\n", sqlite3_errmsg(sqlite3_db_handle(stmt)));
  sqlite3_finalize(stmt);
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
