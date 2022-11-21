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
    DB_exec(bkmkDB, "CREATE TABLE IF NOT EXISTS bookmarks(osm_id INTEGER, list TEXT, props TEXT, notes TEXT, lng REAL, lat REAL);");
    DB_exec(bkmkDB, "CREATE TABLE IF NOT EXISTS saved_views(title TEXT UNIQUE, lng REAL, lat REAL, zoom REAL, rotation REAL, tilt REAL, width REAL, height REAL));");
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
  static int currPlaceIdx = 0;
  static bool updatePlaces = true;

  // using markerSetBitmap will make a copy of bitmap for every marker ... let's see what happens w/ textures
  //  from scene file (I doubt those get duplicated for every use)
  //int markerSize = 24;
  //std::string bkmkMarker = fstring(markerSVG, "#00FFFF", "#000", "0.5");
  //unsigned int* markerImg = rasterizeSVG(bkmkMarker.data(), markerSize, markerSize);

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
      if(bkmkMarkers.empty()) {
        std::string svg = fstring(markerSVG, "#12B5CB");
        app->textureFromSVG("bkmk-marker-cyan", (char*)svg.data(), 1.25f);
      }
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
      std::replace(namestr.begin(), namestr.end(), '"', '\'');
      map->markerSetStylingFromString(bkmkMarkers[markerIdx],
          fstring(searchMarkerStyleStr, "bkmk-marker-cyan", markerIdx+2, namestr.c_str()).c_str());
      map->markerSetPoint(bkmkMarkers[markerIdx], LngLat(lng, lat));
      ++markerIdx;
    }, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, currList.c_str(), -1, SQLITE_STATIC);
    });
    for(; markerIdx < bkmkMarkers.size(); ++markerIdx)
      map->markerSetVisible(bkmkMarkers[markerIdx], false);
    updatePlaces = false;
  }

  std::vector<const char*> cnames;
  for(const auto& s : placeNames)
    cnames.push_back(s.c_str());

  if(ImGui::ListBox("Places", &currPlaceIdx, cnames.data(), cnames.size())) {
    std::string query = fstring("SELECT notes, lng, lat FROM bookmarks WHERE rowid = %d;", placeRowIds[currPlaceIdx]);
    double lng, lat, scrx, scry;
    DB_exec(bkmkDB, query.c_str(), [&](sqlite3_stmt* stmt){
      lng = sqlite3_column_double(stmt, 1);
      lat = sqlite3_column_double(stmt, 2);
      placeNotes = (const char*)(sqlite3_column_text(stmt, 0));
    });
    if(!map->lngLatToScreenPosition(lng, lat, &scrx, &scry))
      map->flyTo(CameraPosition{lng, lat, 16}, 1.0);  // max(map->getZoom(), 14)

    // we should highlight the selected place (while still showing others)
    //markerSetBitmap(MarkerID _marker, int _width, int _height, const unsigned int* _data);
  }

  if (ImGui::Button("Delete Place")) {
    DB_exec(bkmkDB, fstring("DELETE FROM bookmarks WHERE rowid = %d;", placeRowIds[currPlaceIdx]).c_str());
    updatePlaces = true;
  }

  ImGui::InputText("Notes", &placeNotes, ImGuiInputTextFlags_EnterReturnsTrue);

  if (!std::isnan(app->pickResultCoord.latitude) && ImGui::Button("Save Current Place")) {
    const char* strStmt = "INSERT INTO bookmarks (osm_id,list,props,notes,lng,lat) VALUES (?,?,?,?,?,?);";
    sqlite3_stmt* stmt;
    if(sqlite3_prepare_v2(bkmkDB, strStmt, -1, &stmt, NULL) != SQLITE_OK) {
      logMsg("sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(bkmkDB));
      return;
    }
    rapidjson::Document doc;
    doc.Parse(app->pickResultProps.c_str());
    sqlite3_bind_int64(stmt, 1, doc.IsObject() && doc.HasMember("id") ? doc["id"].GetInt64() : 0);
    sqlite3_bind_text(stmt, 2, currList.c_str(), -1, SQLITE_STATIC);  // drop trailing separator
    sqlite3_bind_text(stmt, 3, app->pickResultProps.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, placeNotes.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 5, app->pickResultCoord.longitude);
    sqlite3_bind_double(stmt, 6, app->pickResultCoord.latitude);
    if (sqlite3_step(stmt) != SQLITE_DONE)
      logMsg("sqlite3_step failed: %d\n", sqlite3_errmsg(sqlite3_db_handle(stmt)));
    sqlite3_finalize(stmt);
    updatePlaces = true;
  }
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
    const char* query = "UPDATE INTO saved_views (title,lng,lat,zoom,rotation,tilt,width,height) VALUES (?,?,?,?,?,?,?,?);";

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
