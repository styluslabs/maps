#include "mapsearch.h"
#include "mapsapp.h"
#include "resources.h"
#include "tangram.h"
#include "imgui.h"
#include "imgui_stl.h"

#include <deque>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "data/tileData.h"
#include "scene/scene.h"
#include "sqlite3/sqlite3.h"


// building search DB from tiles
static sqlite3* searchDB = NULL;

static LngLat mapCenter;
static bool sortByDist = false;


void MapsSearch::clearSearch()
{
  for(MarkerID mrkid : searchMarkers)
    app->map->markerSetVisible(mrkid, false);
  for(MarkerID mrkid : dotMarkers)
    app->map->markerSetVisible(mrkid, false);
}

static void udf_osmSearchRank(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  if(argc != 3) {
    sqlite3_result_error(context, "osmSearchRank - Invalid number of arguments (3 required).", -1);
    return;
  }
  if(sqlite3_value_type(argv[0]) != SQLITE_FLOAT || sqlite3_value_type(argv[1]) != SQLITE_FLOAT || sqlite3_value_type(argv[2]) != SQLITE_FLOAT) {
    sqlite3_result_double(context, -1.0);
    return;
  }
  // sqlite FTS5 rank is roughly -1*number_of_words_in_query; ordered from -\inf to 0
  double rank = sortByDist ? -1.0 : sqlite3_value_double(argv[0]);
  double lon = sqlite3_value_double(argv[1]);  // distance from search center point in meters
  double lat = sqlite3_value_double(argv[2]);  // distance from search center point in meters
  double dist = lngLatDist(mapCenter, LngLat(lon, lat));
  // obviously will want a more sophisticated ranking calculation in the future
  sqlite3_result_double(context, rank/log2(1+dist));
}

//search_data:
//    - layer: place
//      fields: name, class

struct SearchData {
  std::string layer;
  std::vector<std::string> fields;
};

static std::vector<SearchData> searchData;
static std::atomic<int> tileCount{};

static void processTileData(TileTask* task, sqlite3_stmt* stmt)
{
  auto tileData = task->source()->parse(*task);
  for(const Layer& layer : tileData->layers) {
    for(const SearchData& searchdata : searchData) {
      if(searchdata.layer == layer.name) {
        for(const Feature& feature : layer.features) {
          if(feature.props.getString("name").empty() || feature.points.empty())
            continue;  // skip POIs w/o name or geometry
          auto lnglat = tileCoordToLngLat(task->tileId(), feature.points.front());
          std::string tags;
          for(const std::string& field : searchdata.fields) {
            tags += feature.props.getString(field);
            tags += ' ';
          }
          // insert row
          sqlite3_bind_text(stmt, 1, tags.c_str(), tags.size() - 1, SQLITE_STATIC);  // drop trailing separator
          sqlite3_bind_text(stmt, 2, feature.props.toJson().c_str(), -1, SQLITE_TRANSIENT);
          sqlite3_bind_double(stmt, 3, lnglat.longitude);
          sqlite3_bind_double(stmt, 4, lnglat.latitude);
          if (sqlite3_step(stmt) != SQLITE_DONE)
            logMsg("sqlite3_step failed: %d\n", sqlite3_errmsg(sqlite3_db_handle(stmt)));
          sqlite3_clear_bindings(stmt);  // not necessary?
          sqlite3_reset(stmt);  // necessary to reuse statement
        }
      }
    }
  }
}

static bool initSearch(Map* map)
{
  static const char* dbPath = "/home/mwhite/maps/fts1.sqlite";
  if(sqlite3_open_v2(dbPath, &searchDB, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK)
    return true;
  sqlite3_close(searchDB);
  searchDB = NULL;

  // load search config
  for(int ii = 0; ii < 100; ++ii) {
    std::string layer = map->readSceneValue(fstring("global.search_data#%d.layer", ii));
    if(layer.empty()) break;
    std::string fieldstr = map->readSceneValue(fstring("global.search_data#%d.fields", ii));
    searchData.push_back({layer, splitStr<std::vector>(fieldstr, ", ", true)});
  }
  if(searchData.empty()) {
    //logMsg("No search fields specified, search will be disabled.\n");
    return false;
  }

  // DB doesn't exist - create it
  if(sqlite3_open_v2(dbPath, &searchDB, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
    logMsg("Error creating %s", dbPath);
    sqlite3_close(searchDB);
    searchDB = NULL;
    return false;
  }

  // get bounds from mbtiles DB
  // mbtiles spec: https://github.com/mapbox/mbtiles-spec/blob/master/1.3/spec.md
  sqlite3* tileDB;
  if(sqlite3_open_v2("/home/mwhite/maps/sf.mbtiles", &tileDB, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
    logMsg("Error opening tile DB: %s\n", sqlite3_errmsg(tileDB));
    return false;
  }
  int min_row, max_row, min_col, max_col, max_zoom;
  const char* boundsSql = "SELECT min(tile_row), max(tile_row), min(tile_column), max(tile_column), max(zoom_level) FROM tiles WHERE zoom_level = (SELECT max(zoom_level) FROM tiles);";
  DB_exec(tileDB, boundsSql, [&](sqlite3_stmt* stmt){
    min_row = sqlite3_column_int(stmt, 0);
    max_row = sqlite3_column_int(stmt, 1);
    min_col = sqlite3_column_int(stmt, 2);
    max_col = sqlite3_column_int(stmt, 3);
    max_zoom = sqlite3_column_int(stmt, 4);
  });
  sqlite3_close(tileDB);

  auto& tileSources = map->getScene()->tileSources();
  auto& tileSrc = tileSources.front();

  //sqlite3_exec(searchDB, "PRAGMA synchronous=OFF", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA count_changes=OFF", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA journal_mode=MEMORY", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA temp_store=MEMORY", NULL, NULL, &errorMessage);
  DB_exec(searchDB, "CREATE VIRTUAL TABLE points_fts USING fts5(tags, props UNINDEXED, lng UNINDEXED, lat UNINDEXED);");
  // search history
  DB_exec(searchDB, "CREATE TABLE history(query TEXT UNIQUE);");

  sqlite3_exec(searchDB, "BEGIN TRANSACTION", NULL, NULL, NULL);
  sqlite3_stmt* stmt;
  char const* strStmt = "INSERT INTO points_fts (tags,props,lng,lat) VALUES (?,?,?,?);";
  if(sqlite3_prepare_v2(searchDB, strStmt, -1, &stmt, NULL) != SQLITE_OK) {
    logMsg("sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(searchDB));
    return false;
  }

  tileCount = (max_row-min_row+1)*(max_col-min_col+1);
  auto tilecb = TileTaskCb{[stmt](std::shared_ptr<TileTask> task) {
    if (task->hasData())
      processTileData(task.get(), stmt);
    if(--tileCount == 0) {
      sqlite3_exec(searchDB, "COMMIT TRANSACTION", NULL, NULL, NULL);
      sqlite3_finalize(stmt);  // then ... stmt = NULL;
      logMsg("Search index built.\n");
    }
  }};

  for(int row = min_row; row <= max_row; ++row) {
    for(int col = min_col; col <= max_col; ++col) {
      TileID tileid(col, (1 << max_zoom) - 1 - row, max_zoom);
      tileSrc->loadTileData(std::make_shared<BinaryTileTask>(tileid, tileSrc), tilecb);
    }
  }

  return true;
}

void MapsSearch::showGUI()
{
  using namespace rapidjson;

  static int resultOffset = 0;
  static std::vector<std::string> autocomplete;
  static std::vector<Document> results;
  static std::vector<LngLat> respts;
  static std::string searchStr;  // imgui compares to this to determine if text is edited, so make persistant
  static int currItem = -1;
  static LngLat dotBounds00, dotBounds11;

  Map* map = app->map;
  if(!searchDB) {
    if(!initSearch(map))
      return;
    // add search ranking fn
    if(sqlite3_create_function(searchDB, "osmSearchRank", 3, SQLITE_UTF8, 0, udf_osmSearchRank, 0, 0) != SQLITE_OK)
      logMsg("sqlite3_create_function: error creating osmSearchRank");
  }
  if(!ImGui::CollapsingHeader("Search", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  LngLat minLngLat(180, 90);
  LngLat maxLngLat(-180, -90);
  bool ent = ImGui::InputText("Query", &searchStr, ImGuiInputTextFlags_EnterReturnsTrue);
  bool edited = ImGui::IsItemEdited();
  // history (autocomplete)
  if(ent) {
    // IGNORE prevents error from UNIQUE constraint
    DB_exec(searchDB, fstring("INSERT OR IGNORE INTO history (query) VALUES ('%s');", searchStr.c_str()).c_str());
  }
  else {
    if(edited) {
      autocomplete.clear();
      std::string histq = fstring("SELECT * FROM history WHERE query LIKE '%s%%' LIMIT 5;", searchStr.c_str());
      DB_exec(searchDB, histq.c_str(), [&](sqlite3_stmt* stmt){
        autocomplete.emplace_back( (const char*)(sqlite3_column_text(stmt, 0)) );
      });
    }
    if(!autocomplete.empty()) {
      std::vector<const char*> cautoc;
      for(const auto& s : autocomplete)
        cautoc.push_back(s.c_str());

      int histItem = -1;
      if(ImGui::ListBox("History", &histItem, cautoc.data(), cautoc.size())) {
        ent = true;
        searchStr = autocomplete[histItem];
        //autocomplete.clear();
      }
    }
  }

  // sort by distance only?
  bool nextPage = false;
  if (ImGui::Checkbox("Sort by distance", &sortByDist)) {
    ent = true;
  }
  if(ImGui::Button("Clear")) {
    //ImGui::SetKeyboardFocusHere(-1);
    if(app->searchActive)
      map->updateGlobals({SceneUpdate{"global.search_active", "false"}});
    for(MarkerID mrkid : dotMarkers)
      map->markerSetVisible(mrkid, false);
    // showBookmarkGUI() will redisplay bookmark markers
    app->searchActive = false;
    searchStr.clear();
    autocomplete.clear();
    ent = true;
  }
  else if (!results.empty()) {
    ImGui::SameLine();
    if(ImGui::Button("More"))
      nextPage = !ent && !edited;
  }

  if(ent || edited || nextPage) {
    if(!nextPage) {
      results.clear();
      respts.clear();
      map->markerSetVisible(app->pickResultMarker, false);
      currItem = -1;
    }
    resultOffset = nextPage ? resultOffset + 20 : 0;
    size_t markerIdx = nextPage ? results.size() : 0;
    if(searchStr.size() > 2) {
      map->getPosition(mapCenter.longitude, mapCenter.latitude);
      // should we add tokenize = porter to CREATE TABLE? seems we want it on query, not content!
      const char* query = "SELECT props, lng, lat FROM points_fts WHERE points_fts "
          "MATCH ? ORDER BY osmSearchRank(rank, lng, lat) LIMIT 20 OFFSET ?;";

      DB_exec(searchDB, query, [&](sqlite3_stmt* stmt){
        double lng = sqlite3_column_double(stmt, 1);
        double lat = sqlite3_column_double(stmt, 2);
        respts.push_back(LngLat(lng, lat));
        results.emplace_back();
        results.back().Parse((const char*)(sqlite3_column_text(stmt, 0)));
        if(!results.back().HasMember("name")) {
          results.pop_back();
          respts.pop_back();
        }
        else if(ent || nextPage) {
          if(searchMarkers.empty()) {
            std::string svg = fstring(markerSVG, "#CF513D");  //"#9A291D"
            app->textureFromSVG("search-marker-red", (char*)svg.data(), 1.25f);
            svg = fstring(markerSVG, "#CF513D");  // SVG parsing is destructive!!!
            app->textureFromSVG("pick-marker-red", (char*)svg.data(), 1.5f);
          }
          if(markerIdx >= searchMarkers.size())
            searchMarkers.push_back(map->markerAdd());
          map->markerSetVisible(searchMarkers[markerIdx], true);
          // 2nd value is priority (smaller number means higher priority)
          std::string namestr = results.back()["name"].GetString();
          std::replace(namestr.begin(), namestr.end(), '"', '\'');
          map->markerSetStylingFromString(searchMarkers[markerIdx],
              fstring(searchMarkerStyleStr, "search-marker-red", markerIdx+2, namestr.c_str()).c_str());
          map->markerSetPoint(searchMarkers[markerIdx], LngLat(lng, lat));
          ++markerIdx;

          if(markerIdx <= 5 || lngLatDist(mapCenter, LngLat(lng, lat)) < 2.0) {
            minLngLat.longitude = std::min(minLngLat.longitude, lng);
            minLngLat.latitude = std::min(minLngLat.latitude, lat);
            maxLngLat.longitude = std::max(maxLngLat.longitude, lng);
            maxLngLat.latitude = std::max(maxLngLat.latitude, lat);
          }
        }
      }, [&](sqlite3_stmt* stmt){
        std::string s(searchStr + "*");
        std::replace(s.begin(), s.end(), '\'', ' ');
        sqlite3_bind_text(stmt, 1, s.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, resultOffset);
      });
    }
    if(!app->searchActive && ent && !results.empty()) {
      map->updateGlobals({SceneUpdate{"global.search_active", "true"}});
      app->mapsBookmarks->hideBookmarks();
      app->searchActive = true;
    }
    for(; markerIdx < searchMarkers.size(); ++markerIdx)
      map->markerSetVisible(searchMarkers[markerIdx], false);
  }

  // dot markers for complete results
  // TODO: also repeat search if zooming in and we were at result limit
  LngLat lngLat00, lngLat11;
  app->getMapBounds(lngLat00, lngLat11);
  if(app->searchActive && (ent || lngLat00.longitude < dotBounds00.longitude || lngLat00.latitude < dotBounds00.latitude
      || lngLat11.longitude > dotBounds11.longitude || lngLat11.latitude > dotBounds11.latitude)) {
    double lng01 = fabs(lngLat11.longitude - lngLat00.longitude);
    double lat01 = fabs(lngLat11.latitude - lngLat00.latitude);
    dotBounds00 = LngLat(lngLat00.longitude - lng01/8, lngLat00.latitude - lat01/8);
    dotBounds11 = LngLat(lngLat11.longitude + lng01/8, lngLat11.latitude + lat01/8);
    size_t markerIdx = 0;
    const char* query = "SELECT rowid, lng, lat FROM points_fts WHERE points_fts "
        "MATCH ? AND lng >= ? AND lat >= ? AND lng <= ? AND lat <= ? ORDER BY rank LIMIT 1000;";
    DB_exec(searchDB, query, [&](sqlite3_stmt* stmt){
      double lng = sqlite3_column_double(stmt, 1);
      double lat = sqlite3_column_double(stmt, 2);
      //respts.push_back(LngLat(lng, lat));
      if(markerIdx >= dotMarkers.size()) {
        dotMarkers.push_back(map->markerAdd());
        map->markerSetStylingFromString(dotMarkers[markerIdx], dotMarkerStyleStr);
      }
      map->markerSetVisible(dotMarkers[markerIdx], true);
      map->markerSetPoint(dotMarkers[markerIdx], LngLat(lng, lat));
      ++markerIdx;
    }, [&](sqlite3_stmt* stmt){
      std::string s(searchStr + "*");
      std::replace(s.begin(), s.end(), '\'', ' ');
      sqlite3_bind_text(stmt, 1, s.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_double(stmt, 2, dotBounds00.longitude);
      sqlite3_bind_double(stmt, 3, dotBounds00.latitude);
      sqlite3_bind_double(stmt, 4, dotBounds11.longitude);
      sqlite3_bind_double(stmt, 5, dotBounds11.latitude);
    });
    for(; markerIdx < dotMarkers.size(); ++markerIdx)
      map->markerSetVisible(dotMarkers[markerIdx], false);
  }

  std::vector<std::string> sresults;
  for (size_t ii = 0; ii < results.size(); ++ii) {
    double distkm = lngLatDist(mapCenter, respts[ii]);
    sresults.push_back(fstring("%s (%.1f km)", results[ii]["name"].GetString(), distkm));
  }

  std::vector<const char*> cresults;
  for(const auto& s : sresults)
    cresults.push_back(s.c_str());

  int prevItem = currItem;
  bool updatePickMarker = false;
  if(app->pickedMarkerId > 0) {
    for(size_t ii = 0; ii < searchMarkers.size(); ++ii) {
      if(searchMarkers[ii] == app->pickedMarkerId) {
        currItem = ii;
        app->pickedMarkerId = 0;
        updatePickMarker = true;
        break;
      }
    }
  }

  double scrx, scry;
  if(ImGui::ListBox("Results", &currItem, cresults.data(), cresults.size()) || updatePickMarker) {
    // if item selected, show info and place single marker
    app->pickLabelStr.clear();
    for (auto& m : results[currItem].GetObject())
      app->pickLabelStr += m.name.GetString() + std::string(" = ") + m.value.GetString() + "\n";
    if(prevItem >= 0)
      map->markerSetVisible(searchMarkers[prevItem], true);
    map->markerSetVisible(searchMarkers[currItem], false);
    if (app->pickResultMarker == 0)
      app->pickResultMarker = map->markerAdd();
    map->markerSetVisible(app->pickResultMarker, true);
    // 2nd value is priority (smaller number means higher priority)
    std::string namestr = results[currItem]["name"].GetString();
    std::replace(namestr.begin(), namestr.end(), '"', '\'');
    map->markerSetStylingFromString(app->pickResultMarker,
        fstring(searchMarkerStyleStr, "pick-marker-red", 1, namestr.c_str()).c_str());
    map->markerSetPoint(app->pickResultMarker, respts[currItem]);

    StringBuffer sb;
    Writer<StringBuffer> writer(sb);
    results[currItem].Accept(writer);
    app->pickResultProps = sb.GetString();
    app->pickResultCoord = respts[currItem];

    // ensure marker is visible
    double lng = respts[currItem].longitude;
    double lat = respts[currItem].latitude;
    if(!map->lngLatToScreenPosition(lng, lat, &scrx, &scry))
      map->flyTo(CameraPosition{lng, lat, 16}, 1.0);  // max(map->getZoom(), 14)
  }
  else if(minLngLat.longitude != 180) {
    map->markerSetVisible(app->pickResultMarker, false);
    app->pickResultCoord = LngLat(NAN, NAN);
    if(!map->lngLatToScreenPosition(minLngLat.longitude, minLngLat.latitude, &scrx, &scry)
        || !map->lngLatToScreenPosition(maxLngLat.longitude, maxLngLat.latitude, &scrx, &scry)) {
      auto pos = map->getEnclosingCameraPosition(minLngLat, maxLngLat);
      pos.zoom = std::min(pos.zoom, 16.0f);
      map->flyTo(pos, 1.0);
    }
  }
}
