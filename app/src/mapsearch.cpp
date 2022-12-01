#include "mapsearch.h"
#include "mapsapp.h"
#include "bookmarks.h"
#include "resources.h"
#include "util.h"
#include "imgui.h"
#include "imgui_stl.h"

#include <deque>
#include "rapidjson/writer.h"
#include "rapidxml/rapidxml.hpp"
#include "data/tileData.h"
#include "data/formats/mvt.h"
#include "scene/scene.h"
#include "sqlite3/sqlite3.h"
#include "isect2d.h"


// building search DB from tiles
static sqlite3* searchDB = NULL;
static sqlite3_stmt* insertStmt = NULL;

static LngLat mapCenter;
static bool sortByDist = false;


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

static void processTileData(TileTask* task, sqlite3_stmt* stmt, const std::vector<SearchData>& searchData)
{
  // TODO: also support GeoJSON and TopoJSON for no source case
  auto tileData = task->source() ? task->source()->parse(*task) : Mvt::parseTile(*task, 0);
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
            logMsg("sqlite3_step failed: %s\n", sqlite3_errmsg(sqlite3_db_handle(stmt)));
          sqlite3_clear_bindings(stmt);  // not necessary?
          sqlite3_reset(stmt);  // necessary to reuse statement
        }
      }
    }
  }
}

void MapsSearch::indexTileData(TileTask* task, int mapId, const std::vector<SearchData>& searchData)
{
  sqlite3_exec(searchDB, "BEGIN TRANSACTION", NULL, NULL, NULL);
  //sqlite3_bind_int(insertStmt, 5, mapId);
  processTileData(task, insertStmt, searchData);
  sqlite3_exec(searchDB, "COMMIT TRANSACTION", NULL, NULL, NULL);
}

//search_data:
//    - layer: place
//      fields: name, class

std::vector<SearchData> MapsSearch::parseSearchFields(const YAML::Node& node)
{
  std::vector<SearchData> searchData;
  for(auto& elem : node)
    searchData.push_back({elem["layer"].Scalar(), splitStr<std::vector>(elem["fields"].Scalar(), ", ", true)});
  return searchData;
}

static bool initSearch()
{
  std::string dbPath = MapsApp::baseDir + "fts1.sqlite";
  if(sqlite3_open_v2(dbPath.c_str(), &searchDB, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
    sqlite3_close(searchDB);
    searchDB = NULL;

    // DB doesn't exist - create it
    if(sqlite3_open_v2(dbPath.c_str(), &searchDB, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
      logMsg("Error creating %s", dbPath.c_str());
      sqlite3_close(searchDB);
      searchDB = NULL;
      return false;
    }

    DB_exec(searchDB, "CREATE VIRTUAL TABLE points_fts USING fts5(tags, props UNINDEXED, lng UNINDEXED, lat UNINDEXED);");
    // search history
    DB_exec(searchDB, "CREATE TABLE history(query TEXT UNIQUE);");
  }
  //sqlite3_exec(searchDB, "PRAGMA synchronous=OFF", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA count_changes=OFF", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA journal_mode=MEMORY", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA temp_store=MEMORY", NULL, NULL, &errorMessage);

  char const* stmtStr = "INSERT INTO points_fts (tags,props,lng,lat) VALUES (?,?,?,?);";
  if(sqlite3_prepare_v2(searchDB, stmtStr, -1, &insertStmt, NULL) != SQLITE_OK) {
    logMsg("sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(searchDB));
    return false;
  }

  if(sqlite3_create_function(searchDB, "osmSearchRank", 3, SQLITE_UTF8, 0, udf_osmSearchRank, 0, 0) != SQLITE_OK)
    logMsg("sqlite3_create_function: error creating osmSearchRank");
  return true;
}

bool MapsSearch::indexMBTiles()
{
  Map* map = app->map;
  YAML::Node searchDataNode;
  YamlPath("global.search_data").get(map->getScene()->config(), searchDataNode);
  auto searchData = parseSearchFields(searchDataNode);
  if(searchData.empty()) {
    //logMsg("No search fields specified, search will be disabled.\n");
    return false;
  }

  for(auto& tileSrc : map->getScene()->tileSources()) {
    if(tileSrc->isRaster()) continue;
    std::string dbfile = tileSrc->offlineInfo().cacheFile;
    if(dbfile.empty()) {
      dbfile = tileSrc->offlineInfo().url;
      if(dbfile.substr(0, 7) == "file://")
        dbfile = dbfile.substr(7);
    }

    // get bounds from mbtiles DB
    // mbtiles spec: https://github.com/mapbox/mbtiles-spec/blob/master/1.3/spec.md
    sqlite3* tileDB;
    if(sqlite3_open_v2(dbfile.c_str(), &tileDB, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
      logMsg("Error opening tile DB: %s\n", sqlite3_errmsg(tileDB));
      continue;
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

    tileCount = (max_row-min_row+1)*(max_col-min_col+1);
    sqlite3_exec(searchDB, "BEGIN TRANSACTION", NULL, NULL, NULL);
    auto tilecb = TileTaskCb{[searchData, this](std::shared_ptr<TileTask> task) {
      if(task->hasData())
        processTileData(task.get(), insertStmt, searchData);
      if(--tileCount == 0) {
        sqlite3_exec(searchDB, "COMMIT TRANSACTION", NULL, NULL, NULL);
        //sqlite3_finalize(insertStmt);  // then ... stmt = NULL;
        logMsg("Search index built.\n");
      }
    }};

    for(int row = min_row; row <= max_row; ++row) {
      for(int col = min_col; col <= max_col; ++col) {
        TileID tileid(col, (1 << max_zoom) - 1 - row, max_zoom);
        tileSrc->loadTileData(std::make_shared<BinaryTileTask>(tileid, tileSrc), tilecb);
      }
    }
    //break; ???
  }

  return true;
}

void MapsSearch::clearSearchResults(std::vector<SearchResult>& results)
{
  for(auto& res : results) {
    if(res.markerId == 0) continue;
    if(res.isPinMarker)
      pinMarkers.push_back(res.markerId);
    else
      dotMarkers.push_back(res.markerId);
    app->map->markerSetVisible(res.markerId, false);
    res.markerId = 0;
  }
  results.clear();
}

void MapsSearch::clearSearch()
{
  clearSearchResults(mapResults);
  clearSearchResults(listResults);

  if(app->searchActive)
    app->map->updateGlobals({SceneUpdate{"global.search_active", "false"}});
  app->searchActive = false;
}

MarkerID MapsSearch::getPinMarker(const SearchResult& res)
{
  Map* map = app->map;
  MarkerID markerId = pinMarkers.empty() ? map->markerAdd() : pinMarkers.back();
  if(!pinMarkers.empty())
    pinMarkers.pop_back();

  map->markerSetVisible(markerId, true);
  // 2nd value is priority (smaller number means higher priority)
  std::string namestr = res.tags["name"].GetString();
  std::replace(namestr.begin(), namestr.end(), '"', '\'');
  map->markerSetStylingFromString(markerId,
      fstring(searchMarkerStyleStr, "search-marker-red", pinMarkerIdx+2, namestr.c_str()).c_str());
  map->markerSetPoint(markerId, res.pos);
  return markerId;
}

MarkerID MapsSearch::getDotMarker(const SearchResult& res)
{
  Map* map = app->map;
  MarkerID markerId = dotMarkers.empty() ? map->markerAdd() : dotMarkers.back();
  if(!dotMarkers.empty())
    dotMarkers.pop_back();
  else
    map->markerSetStylingFromString(markerId, dotMarkerStyleStr);
  map->markerSetVisible(markerId, true);
  map->markerSetPoint(markerId, res.pos);
  return markerId;
}

SearchResult& MapsSearch::addMapResult(int64_t id, double lng, double lat, float rank)
{
  mapResults.push_back({id, {lng, lat}, rank, 0, false, {}});
  return mapResults.back();
}

SearchResult& MapsSearch::addListResult(int64_t id, double lng, double lat, float rank)
{
  listResults.push_back({id, {lng, lat}, rank, 0, false, {}});
  return listResults.back();
}

static isect2d::AABB<glm::vec2> markerAABB(Map* map, LngLat pos, float radius)
{
  double x, y;
  map->lngLatToScreenPosition(pos.longitude, pos.latitude, &x, &y);
  return isect2d::AABB<glm::vec2>(x - radius, y - radius, x + radius, y + radius);
}

// if isect2d is insufficient, try https://github.com/nushoin/RTree - single-header r-tree impl
void MapsSearch::createMarkers()
{
  if(!markerTexturesMade) {
    std::string svg = fstring(markerSVG, "#CF513D");  //"#9A291D"
    app->textureFromSVG("search-marker-red", (char*)svg.data(), 1.25f);
    svg = fstring(markerSVG, "#CF513D");  // SVG parsing is destructive!!!
    app->textureFromSVG("pick-marker-red", (char*)svg.data(), 1.5f);  // slightly bigger
    markerTexturesMade = true;
  }

  isect2d::ISect2D<glm::vec2> collider;
  for(auto& res : mapResults) {
    if(res.markerId == 0) {
      bool collided = false;
      collider.intersect(markerAABB(app->map, res.pos, markerRadius),
          [&](auto& a, auto& b) { collided = true; return false; });
      res.markerId = collided ? getDotMarker(res) : getPinMarker(res);
      res.isPinMarker = !collided;


    }
    else if(res.isPinMarker)
      collider.insert(markerAABB(app->map, res.pos, markerRadius));
  }
}

void MapsSearch::onZoom()
{
  Map* map = app->map;
  float zoom = map->getZoom();
  if(std::abs(zoom - prevZoom) < 0.5f) return;

  double scrx, scry;
  isect2d::ISect2D<glm::vec2> collider;
  if(zoom < prevZoom) {
    // if zoom decr by more than threshold, convert colliding pins to dots
    for(auto& res : mapResults) {
      if(!res.isPinMarker) continue;
      bool collided = false;
      collider.intersect(markerAABB(map, res.pos, markerRadius),
          [&](auto& a, auto& b) { collided = true; return false; });
      if(collided) {
        // convert to dot marker
        pinMarkers.push_back(res.markerId);
        res.markerId = getDotMarker(res);
        res.isPinMarker = false;
      }
    }
  }
  else {
    // if zoom incr, convert dots to pins if no collision
    for(auto& res : mapResults) {
      if(res.isPinMarker) continue;
      // don't touch offscreen markers
      if(!map->lngLatToScreenPosition(res.pos.longitude, res.pos.latitude, &scrx, &scry)) continue;
      bool collided = false;
      collider.intersect(markerAABB(map, res.pos, markerRadius),
          [&](auto& a, auto& b) { collided = true; return false; });
      if(!collided) {
        // convert to pin marker
        dotMarkers.push_back(res.markerId);
        res.markerId = getPinMarker(res);
        res.isPinMarker = true;
      }
    }
  }
  prevZoom = zoom;
}

void MapsSearch::onlineSearch(std::string queryStr, LngLat lngLat00, LngLat lngLat11, bool isMapSearch)
{
  //"exclude_place_ids="
  std::string bounds = fstring("%f,%f,%f,%f", lngLat00.longitude, lngLat00.latitude, lngLat11.longitude, lngLat11.latitude);
  std::string urlStr = fstring("https://nominatim.openstreetmap.org/search?format=jsonv2&viewbox=%s&limit=%d&q=%s",
      bounds.c_str(), isMapSearch ? 50 : 20, Url::escapeReservedCharacters(queryStr).c_str());
  auto url = Url(urlStr);
  app->map->getPlatform().startUrlRequest(url, [this, url, isMapSearch](UrlResponse&& response) {
    if(response.error) {
      logMsg("Error fetching %s: %s\n", url.data().c_str(), response.error);
      return;
    }
    rapidjson::Document doc;
    doc.Parse(response.content.data(), response.content.size());
    for(rapidjson::SizeType ii = 0; ii < doc.Size(); ++ii) {
      int64_t osmid = doc[ii]["osm_id"].GetInt64();
      double lat = doc[ii]["lat"].GetDouble();
      double lng = doc[ii]["lon"].GetDouble();
      float rank = doc[ii]["importance"].GetFloat();

      SearchResult& res = isMapSearch ? addMapResult(osmid, lng, lat, rank) : addListResult(osmid, lng, lat, rank);
      res.tags.AddMember("name", doc[ii]["display_name"], res.tags.GetAllocator());
      res.tags.AddMember(doc[ii]["category"], doc[ii]["type"], res.tags.GetAllocator());
    }

    /*response.content.push_back('\0');
    rapidxml::xml_document<> doc;
    doc.parse<0>(response.content.data());
    auto tag = doc.first_node("searchresults")->first_node("place");
    while(tag) {
      auto lat = tag->first_attribute("lat");
      auto lon = tag->first_attribute("lon");
      auto rank = tag->first_attribute("importance");

      // lat, lon, name, description, photos
      SearchResult& res = addSearchResult(lon->value(), lat->value(), rank->value());
      res.tags.AddMember("name", Value(tag->first_attribute("display_name")->value()), res.tags.GetAllocator());
      res.tags.AddMember(doc[ii]["category"], doc[ii]["type"], res.tags.GetAllocator());

      //pickLabelStr += key->value() + std::string(" = ") + val->value() + std::string("\n");
      tag = tag->next_sibling("place");
    }*/
  });
}

void MapsSearch::onlineMapSearch(std::string queryStr, LngLat lngLat00, LngLat lngLat11)
{
  onlineSearch(queryStr, lngLat00, lngLat11, true);
  moreMapResultsAvail = mapResults.size() >= 50;
}

void MapsSearch::onlineListSearch(std::string queryStr, LngLat lngLat00, LngLat lngLat11)
{
  onlineSearch(queryStr, lngLat00, lngLat11, false);
}

// Remaining issues:
// - we don't want pin markers to disappear (change to dots) when panning

void MapsSearch::offlineMapSearch(std::string queryStr, LngLat lnglat00, LngLat lngLat11)
{
  const char* query = "SELECT rowid, props, lng, lat, rank FROM points_fts WHERE points_fts "
      "MATCH ? AND lng >= ? AND lat >= ? AND lng <= ? AND lat <= ? ORDER BY rank LIMIT 1000;";
  DB_exec(searchDB, query, [&](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 1);
    double lng = sqlite3_column_double(stmt, 2);
    double lat = sqlite3_column_double(stmt, 3);
    double score = sqlite3_column_double(stmt, 4);
    auto& res = addMapResult(rowid, lng, lat, score);
    res.tags.Parse((const char*)(sqlite3_column_text(stmt, 1)));
  }, [&](sqlite3_stmt* stmt){
    std::string s(queryStr + "*");
    std::replace(s.begin(), s.end(), '\'', ' ');
    sqlite3_bind_text(stmt, 1, s.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, lnglat00.longitude);
    sqlite3_bind_double(stmt, 3, lnglat00.latitude);
    sqlite3_bind_double(stmt, 4, lngLat11.longitude);
    sqlite3_bind_double(stmt, 5, lngLat11.latitude);
  });
  moreMapResultsAvail = mapResults.size() >= 1000;
}

void MapsSearch::offlineListSearch(std::string queryStr, LngLat, LngLat)
{
  // should we add tokenize = porter to CREATE TABLE? seems we want it on query, not content!
  const char* query = "SELECT rowid, props, lng, lat, rank FROM points_fts WHERE points_fts "
      "MATCH ? ORDER BY osmSearchRank(rank, lng, lat) LIMIT 20 OFFSET ?;";
  DB_exec(searchDB, query, [&](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 1);
    double lng = sqlite3_column_double(stmt, 2);
    double lat = sqlite3_column_double(stmt, 3);
    double score = sqlite3_column_double(stmt, 4);
    auto& res = addListResult(rowid, lng, lat, score);
    res.tags.Parse((const char*)(sqlite3_column_text(stmt, 1)));
    if(!res.tags.HasMember("name"))
      res.tags.AddMember("name", "Untitled", res.tags.GetAllocator());
  }, [&](sqlite3_stmt* stmt){
    std::string s(queryStr + "*");
    std::replace(s.begin(), s.end(), '\'', ' ');
    sqlite3_bind_text(stmt, 1, s.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, listResults.size());  //resultOffset);
  });
}

void MapsSearch::showGUI()
{
  static std::vector<std::string> autocomplete;
  static std::string searchStr;  // imgui compares to this to determine if text is edited, so make persistant
  static int currItem = -1;
  static int providerIdx = 0;
  static LngLat dotBounds00, dotBounds11;
  static int64_t prevPickedResultId = -1;

  if(!searchDB && !initSearch())
    return;
  if(!ImGui::CollapsingHeader("Search", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  const char* providers[] = {"Offline", "Nominatim"};
  if(ImGui::Combo("Provider", &providerIdx, providers, 2))
    clearSearch();

  Map* map = app->map;
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
    clearSearch();
    searchStr.clear();
    autocomplete.clear();
    ent = true;
  }
  else if (!listResults.empty()) {
    ImGui::SameLine();
    if(ImGui::Button("More"))
      nextPage = !ent && !edited;
  }

  if(ent || edited || nextPage) {
    if(!nextPage) {
      clearSearchResults(mapResults);
      clearSearchResults(listResults);
      map->markerSetVisible(app->pickResultMarker, false);
      currItem = -1;
    }
    //resultOffset = nextPage ? resultOffset + 20 : 0;
    //size_t markerIdx = nextPage ? results.size() : 0;
    if(searchStr.size() > 2) {
      map->getPosition(mapCenter.longitude, mapCenter.latitude);
      if(providerIdx == 1)
        onlineListSearch(searchStr, dotBounds00, dotBounds11);
      else
        offlineListSearch(searchStr, dotBounds00, dotBounds11);

      // zoom out if necessary to show first 5 results
      if(ent || nextPage) {
        LngLat minLngLat(180, 90), maxLngLat(-180, -90);
        int resultIdx = 0;
        for(auto& res : listResults) {
          if(resultIdx <= 5 || lngLatDist(mapCenter, res.pos) < 2.0) {
            minLngLat.longitude = std::min(minLngLat.longitude, res.pos.longitude);
            minLngLat.latitude = std::min(minLngLat.latitude, res.pos.latitude);
            maxLngLat.longitude = std::max(maxLngLat.longitude, res.pos.longitude);
            maxLngLat.latitude = std::max(maxLngLat.latitude, res.pos.latitude);
          }
          ++resultIdx;
        }

        if(minLngLat.longitude != 180) {
          double scrx, scry;
          if(!map->lngLatToScreenPosition(minLngLat.longitude, minLngLat.latitude, &scrx, &scry)
              || !map->lngLatToScreenPosition(maxLngLat.longitude, maxLngLat.latitude, &scrx, &scry)) {
            auto pos = map->getEnclosingCameraPosition(minLngLat, maxLngLat);
            pos.zoom = std::min(pos.zoom, 16.0f);
            map->flyTo(pos, 1.0);
          }
        }
      }

      if(!app->searchActive && ent && !listResults.empty()) {
        map->updateGlobals({SceneUpdate{"global.search_active", "true"}});
        app->mapsBookmarks->hideBookmarks();
        app->searchActive = true;
      }
    }
  }

  LngLat lngLat00, lngLat11;
  app->getMapBounds(lngLat00, lngLat11);
  if(app->searchActive && (ent || (moreMapResultsAvail && map->getZoom() - prevZoom > 0.5f)
      || lngLat00.longitude < dotBounds00.longitude || lngLat00.latitude < dotBounds00.latitude
      || lngLat11.longitude > dotBounds11.longitude || lngLat11.latitude > dotBounds11.latitude)) {
    double lng01 = fabs(lngLat11.longitude - lngLat00.longitude);
    double lat01 = fabs(lngLat11.latitude - lngLat00.latitude);
    dotBounds00 = LngLat(lngLat00.longitude - lng01/8, lngLat00.latitude - lat01/8);
    dotBounds11 = LngLat(lngLat11.longitude + lng01/8, lngLat11.latitude + lat01/8);
    clearSearchResults(mapResults);
    if(providerIdx == 1)
      onlineMapSearch(searchStr, dotBounds00, dotBounds11);
    else
      offlineMapSearch(searchStr, dotBounds00, dotBounds11);
    createMarkers();
    prevZoom = map->getZoom();
  }
  else if(!mapResults.empty() && std::abs(map->getZoom() - prevZoom) > 0.5f) {
    onZoom();
  }

  SearchResult* pickedResult = NULL;
  if(app->pickedMarkerId > 0) {
    for(auto& res : mapResults) {
      if(res.markerId == app->pickedMarkerId) {
        pickedResult = &res;
        map->markerSetVisible(res.markerId, false);  // hide existing marker
        app->pickedMarkerId = 0;
        break;
      }
    }
  }

  std::vector<std::string> sresults;
  for (auto& res : listResults) {
    double distkm = lngLatDist(mapCenter, res.pos);
    sresults.push_back(fstring("%s (%.1f km)", res.tags["name"].GetString(), distkm));
  }

  std::vector<const char*> cresults;
  for(const auto& s : sresults)
    cresults.push_back(s.c_str());

  if(ImGui::ListBox("Results", &currItem, cresults.data(), cresults.size())) {
    pickedResult = &listResults[currItem];
    // find and hide existing map marker, if any
    for(auto& res : mapResults) {
      if(res.id == pickedResult->id)
        map->markerSetVisible(res.markerId, false);
    }
  }

  if(pickedResult) {
    // restore normal marker for previous picked result
    if(prevPickedResultId >= 0) {
      for(auto& res : mapResults) {
        if(res.id == prevPickedResultId)
          map->markerSetVisible(res.markerId, true);
      }
    }
    prevPickedResultId = pickedResult->id;

    app->pickLabelStr.clear();
    for (auto& m : pickedResult->tags.GetObject())
      app->pickLabelStr += m.name.GetString() + std::string(" = ") + m.value.GetString() + "\n";

    if (app->pickResultMarker == 0)
      app->pickResultMarker = map->markerAdd();
    map->markerSetVisible(app->pickResultMarker, true);
    // 2nd value is priority (smaller number means higher priority)
    std::string namestr = pickedResult->tags["name"].GetString();
    std::replace(namestr.begin(), namestr.end(), '"', '\'');
    map->markerSetStylingFromString(app->pickResultMarker,
        fstring(searchMarkerStyleStr, "pick-marker-red", 1, namestr.c_str()).c_str());
    map->markerSetPoint(app->pickResultMarker, pickedResult->pos);

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    pickedResult->tags.Accept(writer);
    app->pickResultProps = sb.GetString();
    app->pickResultCoord = pickedResult->pos;

    // ensure marker is visible
    double scrx, scry;
    double lng = pickedResult->pos.longitude, lat = pickedResult->pos.latitude;
    if(!map->lngLatToScreenPosition(lng, lat, &scrx, &scry))
      map->flyTo(CameraPosition{lng, lat, 16}, 1.0);  // max(map->getZoom(), 14)
  }
}
