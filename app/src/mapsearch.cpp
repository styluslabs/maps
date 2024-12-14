//#define TANGRAM_TRACING
//#include "log.h"
//std::chrono::time_point<std::chrono::system_clock> tangram_log_time_start, tangram_log_time_last;
//std::mutex tangram_log_time_mutex;

#include "mapsearch.h"
#include "mapsapp.h"
#include "bookmarks.h"
#include "plugins.h"
#include "resources.h"
#include "util.h"
#include "mapwidgets.h"

#include <deque>
#include "data/tileData.h"
#include "data/formats/mvt.h"
#include "scene/scene.h"
#include "scene/sceneLoader.h"
#include "scene/styleContext.h"
#include "sqlitepp.h"

#include "usvg/svgpainter.h"
#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"

// building search DB from tiles
SQLiteDB MapsSearch::searchDB;
static sqlite3_stmt* insertStmt = NULL;
static bool hasSearchData = false;

class DummyStyleContext : public Tangram::StyleContext {
public:
  DummyStyleContext() {}  // bypass JSContext creation
};
static DummyStyleContext dummyStyleContext;

static void processTileData(TileTask* task, sqlite3_stmt* stmt, const std::vector<SearchData>& searchData)
{
  using namespace Tangram;
  auto tileData = task->source() ? task->source()->parse(*task) : Mvt::parseTile(*task, 0);
  if(!tileData) return;
  for(const Layer& layer : tileData->layers) {
    for(const SearchData& searchdata : searchData) {
      if(searchdata.layer == layer.name) {
        for(const Feature& feature : layer.features) {
          if(feature.points.empty() || !searchdata.filter.eval(feature, dummyStyleContext))
            continue;
          std::string featname = feature.props.getString("name");
          auto lnglat = tileCoordToLngLat(task->tileId(), feature.points.front());
          std::string tags;
          for(const std::string& field : searchdata.fields) {
            const std::string& s = feature.props.getString(field);
            if(!s.empty())
              tags.append(s).append(" ");
          }
          // insert row
          sqlite3_bind_text(stmt, 1, featname.c_str(), -1, SQLITE_STATIC);
          sqlite3_bind_text(stmt, 2, tags.c_str(), tags.size() - 1, SQLITE_STATIC);  // drop trailing separator
          sqlite3_bind_text(stmt, 3, feature.props.toJson().c_str(), -1, SQLITE_TRANSIENT);
          sqlite3_bind_double(stmt, 4, lnglat.longitude);
          sqlite3_bind_double(stmt, 5, lnglat.latitude);
          if(sqlite3_step(stmt) != SQLITE_DONE)
            LOGE("sqlite3_step failed: %s\n", sqlite3_errmsg(sqlite3_db_handle(stmt)));
          //sqlite3_clear_bindings(stmt);  -- retain binding for tile_id set by caller
          sqlite3_reset(stmt);  // necessary to reuse statement
        }
      }
    }
  }
}

void MapsSearch::indexTileData(TileTask* task, int mapId, const std::vector<SearchData>& searchData)
{
  auto tileId = task->tileId();
  int64_t packedId = packTileId(tileId), cnt = -1;
  if(!searchDB.stmt("SELECT 1 FROM offline_tiles WHERE tile_id = ? LIMIT 1;").bind(packedId).onerow(cnt)) {
    LOGTInit(">>> indexing tile %s", tileId.toString().c_str());
    sqlite3_bind_int64(insertStmt, 6, packedId);  //rowId);  // bind tile_id
    searchDB.exec("BEGIN TRANSACTION");
    processTileData(task, insertStmt, searchData);
    searchDB.exec("COMMIT TRANSACTION");
    LOGT("<<< indexing tile %s", tileId.toString().c_str());
    LOGD("Search indexing completed for tile %s", tileId.toString().c_str());
  }
  searchDB.stmt("INSERT INTO offline_tiles (tile_id, offline_id) VALUES (?,?);").bind(packedId, mapId).exec();
  hasSearchData = true;
}

void MapsSearch::importPOIs(std::string srcuri, int offlineId)
{
  static const char* poiImportSQL = R"#(ATTACH DATABASE '%s' AS poidb;
    BEGIN;
    INSERT INTO main.pois SELECT * FROM poidb.pois;
    INSERT INTO main.offline_tiles SELECT tile_id, %d FROM poidb.pois GROUP BY tile_id;
    COMMIT;
  )#";

  if(searchDB.exec(fstring(poiImportSQL, srcuri.c_str(), offlineId)))
    LOG("POI import from %s completed", srcuri.c_str());
  else
    LOGE("SQL error importing POIs from %s: %s", srcuri.c_str(), searchDB.errMsg());
  // make sure DB is detached even if import fails
  if(!searchDB.exec("DETACH DATABASE poidb;"))
    LOGE("SQL error detaching poidb from search DB: %s", searchDB.errMsg());
}

void MapsSearch::onDelOfflineMap(int mapId)
{
  //DELETE FROM tiles WHERE id IN (SELECT tile_id FROM offline_tiles WHERE offline_id = ? AND tile_id NOT IN (SELECT tile_id FROM offline_tiles WHERE offline_id <> ?));
  // need to use sqlite3_exec for multiple statments in single string
  searchDB.stmt("DELETE FROM offline_tiles WHERE offline_id = ?;").bind(mapId).exec();
  searchDB.stmt("DELETE FROM pois WHERE tile_id NOT IN (SELECT tile_id FROM offline_tiles);").exec();
  //searchDB.stmt("DELETE FROM tiles WHERE id NOT IN (SELECT tile_id FROM offline_tiles);").exec();
}

std::vector<SearchData> MapsSearch::parseSearchFields(const YAML::Node& node)
{
  std::vector<SearchData> searchData;
  for(const auto& elem : node) {
    Tangram::SceneFunctions dummyFns;
    std::vector<std::string> fields;
    for(const auto& field : elem["fields"])
      fields.push_back(field.Scalar());
    auto filter = Tangram::SceneLoader::generateFilter(dummyFns, elem["filter"]);
    if(dummyFns.empty())
      searchData.push_back({elem["layer"].Scalar(), std::move(fields), std::move(filter)});
    else
      LOGE("search_data entry ignored - filters do not support JS functions");
  }
  return searchData;
}

static const char* POI_SCHEMA = R"#(BEGIN;
--CREATE TABLE tiles(id INTEGER PRIMARY KEY, z INTEGER, x INTEGER, y INTEGER, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));
--CREATE UNIQUE INDEX tiles_tile_id ON tiles (z, x, y);
CREATE TABLE offline_tiles(tile_id INTEGER, offline_id INTEGER);
CREATE UNIQUE INDEX offline_index ON offline_tiles (tile_id, offline_id);
CREATE TABLE pois(name TEXT, tags TEXT, props TEXT, lng REAL, lat REAL, tile_id INTEGER);
CREATE VIRTUAL TABLE pois_fts USING fts5(name, tags, content='pois');
CREATE INDEX pois_tile_id ON pois (tile_id);

-- trigger to delete pois when tile row deleted
--CREATE TRIGGER tiles_delete AFTER DELETE ON tiles BEGIN
--  DELETE FROM pois WHERE tile_id = OLD.rowid;
--END;

-- triggers to keep the FTS index up to date.
CREATE TRIGGER pois_insert AFTER INSERT ON pois BEGIN
  INSERT INTO pois_fts(rowid, name, tags) VALUES (NEW.rowid, NEW.name, NEW.tags);
END;
CREATE TRIGGER pois_delete AFTER DELETE ON pois BEGIN
  INSERT INTO pois_fts(pois_fts, rowid, name, tags) VALUES ('delete', OLD.rowid, OLD.name, OLD.tags);
END;
CREATE TRIGGER pois_update AFTER UPDATE ON pois BEGIN
  INSERT INTO pois_fts(pois_fts, rowid, name, tags) VALUES ('delete', OLD.rowid, OLD.name, OLD.tags);
  INSERT INTO pois_fts(rowid, name, tags) VALUES (NEW.rowid, NEW.name, NEW.tags);
END;
COMMIT;)#";

bool MapsSearch::initSearch()
{
  FSPath dbPath(MapsApp::baseDir, "fts1.sqlite");
  if(sqlite3_open_v2(dbPath.c_str(), &searchDB.db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
    sqlite3_close(searchDB.release());

    // DB doesn't exist - create it
    if(sqlite3_open_v2(dbPath.c_str(), &searchDB.db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
      LOGE("Error creating %s", dbPath.c_str());
      sqlite3_close(searchDB.release());
      return false;
    }

    searchDB.exec(POI_SCHEMA);
    // search history - NOCASE causes comparisions to be case-insensitive but still stores case
    searchDB.exec("CREATE TABLE history(query TEXT UNIQUE COLLATE NOCASE, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));");
  }
  //sqlite3_exec(searchDB, "PRAGMA synchronous=OFF; PRAGMA count_changes=OFF; PRAGMA journal_mode=MEMORY; PRAGMA temp_store=MEMORY", NULL, NULL, &errorMessage);

  char const* stmtStr = "INSERT INTO pois (name,tags,props,lng,lat,tile_id) VALUES (?,?,?,?,?,?);";
  if(sqlite3_prepare_v2(searchDB.db, stmtStr, -1, &insertStmt, NULL) != SQLITE_OK) {
    LOGE("sqlite3_prepare_v2 error: %s\n", searchDB.errMsg());
    return false;
  }

  if(sqlite3_create_function(searchDB.db, "osmSearchRank", 3, SQLITE_UTF8, 0, udf_osmSearchRank, 0, 0) != SQLITE_OK)
    LOGE("sqlite3_create_function: error creating osmSearchRank for search DB");

  int npois = 0;
  searchDB.stmt("SELECT COUNT(1) FROM pois;").onerow(npois);
  hasSearchData = npois > 0;

  return true;
}

MapsSearch::~MapsSearch() {}

MapsSearch::MapsSearch(MapsApp* _app) : MapsComponent(_app)
{
  initSearch();
}

bool MapsSearch::indexMBTiles()
{
  Map* map = app->map.get();
  const YAML::Node& searchDataNode = map->getScene()->config()["application"]["search_data"];
  auto searchData = parseSearchFields(searchDataNode);
  if(searchData.empty()) {
    LOGW("No search fields specified, cannot build index!\n");
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
      LOGE("Error opening tile DB: %s\n", sqlite3_errmsg(tileDB));
      sqlite3_close(tileDB);
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
    searchDB.exec("BEGIN TRANSACTION");
    auto tilecb = TileTaskCb{[searchData, this](std::shared_ptr<TileTask> task) {
      if(task->hasData())
        processTileData(task.get(), insertStmt, searchData);
      if(--tileCount == 0) {
        searchDB.exec("COMMIT TRANSACTION");
        //sqlite3_finalize(insertStmt);  // then ... stmt = NULL;
        LOG("Search index built.\n");
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

void MapsSearch::clearSearchResults()
{
  app->pluginManager->cancelRequests(PluginManager::SEARCH);  // cancel any outstanding search requests
  app->gui->deleteContents(resultsContent, ".listitem");
  listResultOffset = 0;
  resultCountText->setText(" ");  // use non-empty string to maintain layout height
  mapResults.clear();
  listResults.clear();
  moreMapResultsAvail = false;
  moreListResultsAvail = false;
  markers->reset();
  flyingToResults = false;  // just in case event got dropped
  saveToBkmksBtn->setEnabled(false);
}

void MapsSearch::clearSearch()
{
  clearSearchResults();
  queryText->setText("");
  if(app->searchActive) {
    //app->map->updateGlobals({SceneUpdate{"global.search_active", "false"}});
    app->map->getScene()->hideExtraLabels = false;
    app->mapsBookmarks->restoreBookmarks();
  }
  app->searchActive = false;
}

void MapsSearch::addMapResult(int64_t id, double lng, double lat, float rank, const char* json)
{
  // for online searches, we don't want to clear previous results until we get new results
  if(newMapSearch) {
    mapResults.clear();
    markers->reset();
    newMapSearch = false;
  }
  size_t idx = mapResults.size();
  mapResults.push_back({id, {lng, lat}, rank, json});
  auto onPicked = [this, idx](){
    SearchResult& res = mapResults[idx];
    app->setPickResult(res.pos, "", res.tags);
  };
  markers->createMarker({lng, lat}, onPicked, jsonToProps(json));
  if(idx == 0)
    saveToBkmksBtn->setEnabled(true);
}

void MapsSearch::addListResult(int64_t id, double lng, double lat, float rank, const char* json)
{
  listResults.push_back({id, {lng, lat}, rank, json});
}

void MapsSearch::searchPluginError(const char* err)
{
  retryBtn->setIcon(MapsApp::uiIcon("retry"));
  retryBtn->setVisible(true);
  resultCountText->setText("Search failed");
}

void MapsSearch::offlineMapSearch(std::string queryStr, LngLat lnglat00, LngLat lngLat11)
{
  const char* query = "SELECT pois.rowid, lng, lat, rank, props FROM pois_fts JOIN pois ON pois.ROWID = pois_fts.ROWID WHERE pois_fts "
      "MATCH ? AND pois.lng >= ? AND pois.lat >= ? AND pois.lng <= ? AND pois.lat <= ? ORDER BY rank LIMIT 1000;";
  searchDB.stmt(query)
      .bind(queryStr, lnglat00.longitude, lnglat00.latitude, lngLat11.longitude, lngLat11.latitude)
      .exec([&](int rowid, double lng, double lat, double score, const char* json){
    addMapResult(rowid, lng, lat, score, json);
  });
  moreMapResultsAvail = mapResults.size() >= 1000;
}

void MapsSearch::offlineListSearch(std::string queryStr, LngLat, LngLat)
{
  // if results don't fill height, scroll area won't scroll, so onScroll won't be called to get more results!
  int limit = std::max(20, int(app->win->winBounds().height()/42 + 1));
  int offset = listResults.size();
  // if '*' not appended to string, we assume catagorical search - no info for ranking besides dist
  bool sortByDist = queryStr.back() != '*' || app->config["search"]["sort"].as<std::string>("rank") == "dist";
  // should we add tokenize = porter to CREATE TABLE? seems we want it on query, not content!
  std::string query = fstring("SELECT pois.rowid, lng, lat, rank, props FROM pois_fts JOIN pois ON"
      " pois.ROWID = pois_fts.ROWID WHERE pois_fts MATCH ? ORDER BY osmSearchRank(%s, lng, lat) LIMIT %d OFFSET ?;",
      sortByDist ? "-1.0" : "rank", limit);
  searchDB.stmt(query).bind(queryStr, offset).exec([&](int rowid, double lng, double lat,
      double score, const char* json){ addListResult(rowid, lng, lat, score, json); });
  moreListResultsAvail = int(listResults.size()) - offset >= limit;
}

void MapsSearch::onMapEvent(MapEvent_t event)
{
  if(!app->searchActive) return;
  if(event == MARKER_PICKED) {
    if(app->pickedMarkerId > 0) {
      if(markers->onPicked(app->pickedMarkerId))
        app->pickedMarkerId = 0;
    }
    return;
  }
  if(event != MAP_CHANGE) return;

  Map* map = app->map.get();
  LngLat lngLat00, lngLat11;
  app->getMapBounds(lngLat00, lngLat11);
  if(flyingToResults && !app->mapState.isAnimating()) {  //event == CAMERA_EASE_DONE
    flyingToResults = false;
    if(unifiedSearch) {  // need to perform map search in new location if not unified!
      updateMapResultBounds(lngLat00, lngLat11); // update bounds for new camera position
      return;
    }
  }
  float zoom = map->getZoom();
  bool zoomedin = zoom - prevZoom > 0.5f;
  bool zoomedout = zoom - prevZoom < -0.5f;
  bool mapmoved = lngLat00.longitude < dotBounds00.longitude || lngLat00.latitude < dotBounds00.latitude
      || lngLat11.longitude > dotBounds11.longitude || lngLat11.latitude > dotBounds11.latitude;
  // don't search until animation stops
  if(searchOnMapMove && !app->mapState.isAnimating() && (mapmoved || (moreMapResultsAvail && zoomedin))) {
    updateMapResults(lngLat00, lngLat11, MAP_SEARCH);
    prevZoom = zoom;
  }
  else if(!mapResults.empty() && (zoomedin || zoomedout))
    prevZoom = zoom;
  // any map pan or zoom can potentially affect ranking of list results
  if(mapmoved || zoomedin || zoomedout)
    retryBtn->setVisible(true);

  // make sure extra labels still hidden if scene reloaded or source changed
  map->getScene()->hideExtraLabels = zoom < app->config["search"]["min_poi_zoom"].as<float>(19);
}

void MapsSearch::updateMapResultBounds(LngLat lngLat00, LngLat lngLat11)
{
  double lng01 = fabs(lngLat11.longitude - lngLat00.longitude);
  double lat01 = fabs(lngLat11.latitude - lngLat00.latitude);
  dotBounds00 = LngLat(lngLat00.longitude - lng01/8, lngLat00.latitude - lat01/8);
  dotBounds11 = LngLat(lngLat11.longitude + lng01/8, lngLat11.latitude + lat01/8);
}

void MapsSearch::updateMapResults(LngLat lngLat00, LngLat lngLat11, int flags)
{
  updateMapResultBounds(lngLat00, lngLat11);
  // should we do clearJsSearch() to prevent duplicate results?
  newMapSearch = true;
  if(providerIdx > 0)
    app->pluginManager->jsSearch(providerIdx - 1, searchStr, dotBounds00, dotBounds11, flags);
  else
    offlineMapSearch(searchStr, dotBounds00, dotBounds11);
}

void MapsSearch::resultsUpdated(int flags)
{
  populateResults();
  int nresults = std::max(mapResults.size(), listResults.size());
  bool more = mapResults.size() > listResults.size() ? moreMapResultsAvail : moreListResultsAvail;
  resultCountText->setText(nresults == 1 ? "1 result" :
      fstring("%s%d results", more ? "over " : "" , nresults).c_str());

  // zoom out if necessary to show first 5 results
  if(flags & FLY_TO) {
    Map* map = app->map.get();
    LngLat minLngLat(180, 90), maxLngLat(-180, -90);
    for(size_t ii = 0; ii < listResults.size(); ++ii) {
      auto& res = listResults[ii];
      if(ii < 5 || lngLatDist(app->getMapCenter(), res.pos) < 2.0) {
        minLngLat.longitude = std::min(minLngLat.longitude, res.pos.longitude);
        minLngLat.latitude = std::min(minLngLat.latitude, res.pos.latitude);
        maxLngLat.longitude = std::max(maxLngLat.longitude, res.pos.longitude);
        maxLngLat.latitude = std::max(maxLngLat.latitude, res.pos.latitude);
      }
    }

    if(minLngLat.longitude != 180) {
      double scrx, scry;
      if(!map->lngLatToScreenPosition(minLngLat.longitude, minLngLat.latitude, &scrx, &scry)
          || !map->lngLatToScreenPosition(maxLngLat.longitude, maxLngLat.latitude, &scrx, &scry)) {
        auto pos = map->getEnclosingCameraPosition(minLngLat, maxLngLat, {32});
        pos.zoom = std::min(pos.zoom, 16.0f);
        pos.tilt = map->getTilt();
        pos.rotation = map->getRotation();
        app->gotoCameraPos(pos);  //map->flyTo(pos, 1.0);
        flyingToResults = true;  // has to be set after flyTo()
      }
    }
  }
}

// called externally, e.g., for geo: URIs
void MapsSearch::doSearch(std::string query)
{
  if(query.substr(0, 4) == "geo:") {
    Url uri(query);
    query = uri.path();  // use lat,lng if no query string
    if(uri.hasQuery()) {
      auto qparams = splitStr<std::vector>(uri.query(), "&");
      for(auto& qparam : qparams) {
        if(qparam.substr(0,2) == "q=") {
          query = Url::unEscapeReservedCharacters(qparam.substr(2));
          break;
        }
      }
    }
  }
  app->showPanel(searchPanel);
  queryText->setText(query.c_str());
  searchText(query, RETURN);
}

void MapsSearch::searchText(std::string query, SearchPhase phase)
{
  Map* map = app->map.get();
  LngLat lngLat00, lngLat11;
  app->getMapBounds(lngLat00, lngLat11);
  query = trimStr(query);
  if(phase != NEXTPAGE) {
    // add synonyms to query (e.g., add "fast food" to "restaurant" query)
    if(providerIdx == 0 && !query.empty()) {
      // jsCallFn will return empty string in case of error
      std::string tfquery = phase == RETURN ? app->pluginManager->jsCallFn("transformQuery", query) : "";
      if(!tfquery.empty())
        searchStr = tfquery;
      else {
        // words containing any special characters need to be quoted, so just quote every word (and make AND
        //  operation explicit)
        auto words = splitStr<std::vector>(query, " ", true);
        searchStr = "\"" + joinStr(words, "\" AND \"") + "\"*";
      }
      //std::replace(searchStr.begin(), searchStr.end(), '\'', ' ');
      LOGD("Search string: %s", searchStr.c_str());
    }
    else
      searchStr = query;
    clearSearchResults();
    map->markerSetVisible(app->pickResultMarker, false);
    app->showPanel(searchPanel);
    cancelBtn->setVisible(!searchStr.empty());
  }
  retryBtn->setVisible(false);
  retryBtn->setIcon(MapsApp::uiIcon("refresh"));  // error cleared

  // use map center for origin if current location is offscreen
  if(phase != NEXTPAGE) {
    LngLat loc = app->currLocation.lngLat();
    isCurrLocDistOrigin = map->lngLatToScreenPosition(loc.longitude, loc.latitude);
    searchRankOrigin = isCurrLocDistOrigin ? loc : app->getMapCenter();
  }

  if(phase == EDITING) {
    std::vector<std::string> autocomplete;
    searchDB.stmt("SELECT query FROM history WHERE query LIKE ? ORDER BY timestamp DESC LIMIT ?;")
        .bind(query + "%", query.size() > 1 && providerIdx == 0 ? 5 : 25) // LIMIT 5 to leave room for results
        .exec([&](const char* q){ autocomplete.emplace_back(q); });
    populateAutocomplete(autocomplete);
    if(query.size() > 1 && providerIdx == 0) {  // 2 chars for latin, 1-2 for non-latin (e.g. Chinese)
      offlineListSearch("name:(" + searchStr + ")", lngLat00, lngLat11);  // restrict live search to name
      resultsUpdated(0);
    }
    return;
  }

  if(phase == RETURN && query.size() > 1) {
    searchDB.stmt("INSERT OR REPLACE INTO history (query) VALUES (?);").bind(query).exec();
    // handle lat,lng string
    if(isDigit(query[0]) || query[0] == '-') {
      LngLat pos = parseLngLat(query.c_str());
      if(!std::isnan(pos.latitude) && !std::isnan(pos.longitude)) {
        // if we close search, no way to edit lat,lng from history
        //if(app->panelHistory.size() == 1)
        //  app->popPanel();  // just close search
        app->setPickResult(pos, "", "");
        return;
      }
    }
    if(providerIdx == 0 || !unifiedSearch)
      updateMapResults(lngLat00, lngLat11, MAP_SEARCH);
  }

  if(providerIdx == 0) {
    offlineListSearch(searchStr, lngLat00, lngLat11);
    resultsUpdated(phase == RETURN ? FLY_TO : 0);
  }
  else {
    bool sortByDist = app->config["search"]["sort"].as<std::string>("rank") == "dist";
    int flags = LIST_SEARCH | (phase == RETURN ? FLY_TO : 0) | (sortByDist ? SORT_BY_DIST : 0);
    resultCountText->setText("Searching...");
    if(unifiedSearch)
      updateMapResults(lngLat00, lngLat11, flags | MAP_SEARCH);
    else
      app->pluginManager->jsSearch(providerIdx - 1, searchStr, lngLat00, lngLat11, flags);
  }

  if(phase == RETURN) {
    app->gui->setFocused(resultsContent);
    app->maximizePanel(false);
  }

  if(!app->searchActive && phase == RETURN) {
    //map->updateGlobals({SceneUpdate{"global.search_active", "true"}});
    map->getScene()->hideExtraLabels = map->getZoom() < app->config["search"]["min_poi_zoom"].as<float>(19);
    if(app->config["search"]["hide_bookmarks"].as<bool>(false))
      app->mapsBookmarks->hideBookmarks();  // also hide tracks?
    app->searchActive = true;
  }
}

void MapsSearch::populateAutocomplete(const std::vector<std::string>& history)
{
  for(size_t ii = 0; ii < history.size(); ++ii) {
    std::string query = history[ii];
    Button* item = createListItem(MapsApp::uiIcon("clock"), history[ii].c_str());
    Widget* container = item->selectFirst(".child-container");
    item->onClicked = [=](){
      queryText->setText(query.c_str());
      searchText(query, RETURN);
    };

    Button* discardBtn = createToolbutton(MapsApp::uiIcon("discard"), "Remove");
    discardBtn->onClicked = [=](){
      searchDB.stmt("DELETE FROM history WHERE query = ?;").bind(query).exec();
      searchText(queryText->text(), EDITING);  // refresh
    };
    container->addWidget(discardBtn);
    item->node->setAttribute("__querytext", history[ii].c_str());
    resultsContent->addWidget(item);
  }
}

void MapsSearch::populateResults()
{
  static const char* distProtoSVG = R"#(
    <g margin="0 0" layout="flex" flex-direction="row">
      <use class="icon gps-location" display="none" width="16" height="16" xlink:href=":/ui-icons.svg#gps-location" />
      <use class="icon crosshair" display="none" width="16" height="16" xlink:href=":/ui-icons.svg#crosshair" />
      <text class="weak" font-size="12" margin="0 8 0 6"></text>
    </g>
  )#";
  static std::unique_ptr<SvgNode> distProto;
  if(!distProto)
    distProto.reset(loadSVGFragment(distProtoSVG));

  for(size_t ii = listResultOffset; ii < listResults.size(); ++ii) {  //for(const auto& res : results)
    const SearchResult& res = listResults[ii];
    Properties props = jsonToProps(res.tags.c_str());
    std::string namestr = app->getPlaceTitle(props);
    std::string placetype = !res.tags.empty() ? app->pluginManager->jsCallFn("getPlaceType", res.tags) : "";
    if(namestr.empty()) namestr.swap(placetype);  // we can show type instead of name if present
    if(namestr.empty()) continue;  // skip if nothing to show in list
    Button* item = createListItem(MapsApp::uiIcon("search"), namestr.c_str(), placetype.c_str());
    item->onClicked = [this, ii](){
      app->setPickResult(listResults[ii].pos, "", listResults[ii].tags);
    };

    // show distance to search origin
    Widget* distWidget = new Widget(distProto->clone());
    distWidget->selectFirst(isCurrLocDistOrigin ? ".gps-location" : ".crosshair")->setVisible(true);
    double distkm = lngLatDist(searchRankOrigin, res.pos);
    distWidget->setText(MapsApp::distKmToStr(distkm, 1, 3).c_str());  // 3 sig digits so no decimal over 100km
    item->selectFirst(".child-container")->addWidget(distWidget);
    item->node->setAttribute("__querytext", namestr.c_str());
    resultsContent->addWidget(item);
  }
  listResultOffset = listResults.size();
}

Button* MapsSearch::createPanel()
{
  static const char* searchHeaderSVG = R"#(
    <g box-anchor="hfill" layout="flex" flex-direction="column">
      <g class="searchbox inputbox toolbar" box-anchor="hfill" layout="box">
        <rect class="toolbar-bg background" box-anchor="vfill" width="250" height="20"/>
        <rect class="inputbox-bg" box-anchor="fill" width="150" height="36"/>
        <g class="searchbox_content child-container" box-anchor="hfill" layout="flex" flex-direction="row">
          <g class="toolbutton search-btn" layout="box">
            <rect class="background" box-anchor="hfill" width="36" height="34"/>
            <use class="icon" width="30" height="30" xlink:href=":/ui-icons.svg#search"/>
          </g>
          <g class="textbox searchbox_text" box-anchor="hfill" layout="box">
            <rect class="min-width-rect" fill="none" width="150" height="36"/>
          </g>
          <g class="toolbutton retry-btn" display="none" layout="box">
            <rect class="background" box-anchor="hfill" width="36" height="34"/>
            <use class="icon" width="30" height="30" xlink:href=":/ui-icons.svg#retry"/>
          </g>
          <g class="toolbutton cancel-btn" display="none" layout="box">
            <rect class="background" box-anchor="hfill" width="36" height="34"/>
            <use class="icon" width="26" height="26" xlink:href=":/ui-icons.svg#circle-x"/>
          </g>
        </g>
        <rect class="noquery-overlay" display='none' fill='none' box-anchor='fill' width='20' height='20'/>
      </g>
      <g box-anchor="hfill" layout="flex" flex-direction="column">
        <text class="result-count-text" box-anchor="right" margin="0 5" font-size="12"></text>
        <rect class="separator" width="20" height="2" box-anchor="hfill"/>
      </g>
    </g>
  )#";

  SvgG* searchHeaderNode = static_cast<SvgG*>(loadSVGFragment(searchHeaderSVG));
  SvgG* searchBoxNode = static_cast<SvgG*>(searchHeaderNode->selectFirst(".searchbox"));
  SvgG* textEditNode = static_cast<SvgG*>(searchBoxNode->selectFirst(".textbox"));
  Widget* searchHeader = new Widget(searchHeaderNode);
  textEditNode->addChild(textEditInnerNode());
  queryText = new TextEdit(textEditNode);
  setMinWidth(queryText, 100);

  Widget* searchBox = new Widget(searchBoxNode);
  setupFocusable(searchBox);  //queryText->isFocusable = false; searchBox->isFocusable = true;
  searchBox->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(SvgGui::isFocusedWidgetEvent(event))
      return queryText->sdlEvent(gui, event);
    return false;
  });

  resultCountText = new TextBox(searchHeaderNode->selectFirst(".result-count-text"));
  resultCountText->setText(" ");  // prevent layout from collapsing

  SvgText* msgnode = createTextNode("No offline search data. Tap here to download or select a different search plugin.");
  Button* noDataMsg = new Button(msgnode);
  noDataMsg->onClicked = [](){ MapsApp::openURL(MapsApp::config["search"]["download_url"].as<std::string>("").c_str()); };
  searchHeader->addWidget(noDataMsg);
  msgnode->setText(SvgPainter::breakText(msgnode, 250).c_str());  //app->getPanelWidth() - 70
  noDataMsg->setVisible(false);

  SvgNode* overlayNode = searchBoxNode->selectFirst(".noquery-overlay");
  Button* textEditOverlay = new Button(overlayNode);
  textEditOverlay->onClicked = [=](){ searchText("", RETURN); };

  queryText->onChanged = [this](const char* s){
    selectedResultIdx = -1;  // no need to clear checked item since all contents will be deleted
    searchText(s, EDITING);
  };
  queryText->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_KEYDOWN) {
      if(event->key.keysym.sym == SDLK_RETURN) {
        auto& resultNodes = resultsContent->containerNode()->children();
        if(selectedResultIdx >= 0 && selectedResultIdx < resultNodes.size()) {
          auto it = resultNodes.begin();
          std::advance(it, selectedResultIdx);
          static_cast<Button*>((*it)->ext())->onClicked();
        }
        else if(!queryText->text().empty())
          searchText(queryText->text(), RETURN);
        return true;
      }
      if(event->key.keysym.sym == SDLK_DOWN || event->key.keysym.sym == SDLK_UP) {
        auto& resultNodes = resultsContent->containerNode()->children();
        int n = resultNodes.size();
        if(selectedResultIdx < 0 || selectedResultIdx >= n)
          selectedResultIdx = event->key.keysym.sym == SDLK_DOWN ? 0 : n - 1;
        else {
          auto it = resultNodes.begin();
          std::advance(it, selectedResultIdx);
          static_cast<Button*>((*it)->ext())->setChecked(false);
          int step = event->key.keysym.sym == SDLK_DOWN ? 1 : n - 1;
          selectedResultIdx = (selectedResultIdx + step)%n;
        }
        auto it = resultNodes.begin();
        std::advance(it, selectedResultIdx);
        static_cast<Button*>((*it)->ext())->setChecked(true);
        queryText->setText((*it)->getStringAttr("__querytext", ""));
        sendKeyPress(gui, queryText, SDLK_END);  // move cursor to end of text
        return true;
      }
    }
    else if(event->type == SvgGui::FOCUS_GAINED) {
      app->maximizePanel(true);
      retryBtn->setVisible(false);  // we could consider keeping visible
    }
    return false;
  });

  // this btn clears search text w/o closing panel (use back btn to close panel)
  cancelBtn = new Button(searchBoxNode->selectFirst(".cancel-btn"));
  cancelBtn->isFocusable = true;
  cancelBtn->onClicked = [=](){
    clearSearch();
    app->gui->setFocused(searchBox);  //queryText);  // cancelBtn won't be visible if text input disabled
    searchText("", EDITING);  // show history
  };

  retryBtn = new Button(searchBoxNode->selectFirst(".retry-btn"));
  retryBtn->isFocusable = true;
  retryBtn->onClicked = [this](){
    if(!queryText->text().empty())
      searchText(queryText->text(), RETURN);
  };

  auto onSetProvider = [=](int idx) {
    noDataMsg->setVisible(idx == 0 && !hasSearchData);
    std::string typestr = idx > 0 ? app->pluginManager->searchFns[idx-1].type : "";
    StringRef type(typestr);
    searchOnMapMove = !type.contains("-slow");
    unifiedSearch = type.contains("-unified");
    bool noquery = type.contains("-noquery");
    if(noquery)
      queryText->setText("");
    queryText->setEmptyText(noquery ? "Tap to update" : "");
    queryText->setEnabled(!noquery);
    textEditOverlay->setVisible(noquery);  //&& slow?
  };

  std::vector<std::string> cproviders = {"Offline Search"};
  for(auto& fn : app->pluginManager->searchFns)
    cproviders.push_back(fn.title.c_str());

  providerIdx = std::min(int(cproviders.size())-1, app->config["search"]["plugin"].as<int>(0));
  auto searchTb = app->createPanelHeader(MapsApp::uiIcon("search"), cproviders[providerIdx].c_str());
  bool hasPlugins = !app->pluginManager->searchFns.empty();
  Button* searchPluginBtn = createToolbutton(MapsApp::uiIcon(hasPlugins ? "plugin" : "no-plugin"), "Plugin");
  searchPluginBtn->setEnabled(hasPlugins);
  Menu* searchPluginMenu = createMenu(Menu::VERT_LEFT, false);
  for(size_t ii = 0; ii < cproviders.size(); ++ii) {
    std::string title = cproviders[ii];
    searchPluginMenu->addItem(title.c_str(), [=](){
      static_cast<TextLabel*>(searchPanel->selectFirst(".panel-title"))->setText(title.c_str());
      app->config["search"]["plugin"] = providerIdx = ii;
      onSetProvider(ii);
      app->gui->setFocused(queryText->isEnabled() ? (Widget*)searchBox : (Widget*)textEditOverlay);
      if(queryText->isEnabled() && queryText->text().empty()) {
        clearSearch();
        searchText("", EDITING);  // show history
      }
      else
        searchText(queryText->text(), RETURN);
    });
  }
  searchPluginBtn->setMenu(searchPluginMenu);
  searchTb->addWidget(searchPluginBtn);
  onSetProvider(providerIdx);

  // result sort order
  static const char* resultSortKeys[] = {"rank", "dist"};
  std::string initSort = app->config["search"]["sort"].as<std::string>("rank");
  size_t initSortIdx = 0;
  while(initSortIdx < 2 && initSort != resultSortKeys[initSortIdx]) ++initSortIdx;
  Menu* sortMenu = createRadioMenu({"Relevence", "Distance"}, [this](size_t ii){
    app->config["search"]["sort"] = resultSortKeys[ii];
    if(!queryText->text().empty())
      searchText(queryText->text(), RETURN);
  }, initSortIdx);
  Button* sortBtn = createToolbutton(MapsApp::uiIcon("sort"), "Sort");
  sortBtn->setMenu(sortMenu);
  searchTb->addWidget(sortBtn);

  Button* overflowBtn = createToolbutton(MapsApp::uiIcon("overflow"), "More");
  Menu* overflowMenu = createMenu(Menu::VERT_LEFT, false);
  overflowBtn->setMenu(overflowMenu);
  saveToBkmksBtn = overflowMenu->addItem("Save as bookmarks", [=](){
    std::string plugin = cproviders[providerIdx];
    std::string query = queryText->text();
    app->mapsBookmarks->createFromSearch(query.empty() ? plugin : plugin + ": " + query, mapResults);
    app->showPanel(app->mapsBookmarks->listsPanel, false);
  });
  searchTb->addWidget(overflowBtn);

  resultsContent = createColumn();
  searchPanel = app->createMapPanel(searchTb, resultsContent, searchHeader);

  searchPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == MapsApp::PANEL_OPENED) {
      if(queryText->isEnabled()) {
        noDataMsg->setVisible(providerIdx == 0 && !hasSearchData);
        app->gui->setFocused(searchBox);
        searchText("", EDITING);  // show history
      }
      else if(providerIdx > 0 && !queryText->isEnabled())
        searchText("", RETURN);
    }
    else if(event->type == MapsApp::PANEL_CLOSED)
      clearSearch();
    else if(event->type == SvgGui::INVISIBLE)
      app->maximizePanel(false);
    return false;
  });

  auto scrollWidget = static_cast<ScrollWidget*>(resultsContent->parent());
  scrollWidget->onScroll = [=](){
    // get more list results
    if(moreListResultsAvail && scrollWidget->scrollY >= scrollWidget->scrollLimits.bottom)
      searchText("", NEXTPAGE);
  };

  markers.reset(new MarkerGroup(app->map.get(),
      "layers.search-marker.draw.marker", "layers.search-dot.draw.marker"));

  // main toolbar button
  Menu* searchMenu = createMenu(Menu::VERT);
  //searchMenu->autoClose = true;
  searchMenu->addHandler([this, searchMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(searchMenu->selectFirst(".child-container"));
      if(providerIdx > 0 && !queryText->isEnabled()) {  // noquery plugin
        const char* title = app->pluginManager->searchFns[providerIdx-1].title.c_str();
        searchMenu->addItem(title, MapsApp::uiIcon("search"), [=](){ app->showPanel(searchPanel); });
      }
      else {
        // TODO: pinned searches - timestamp column = INF?
        int uiWidth = app->getPanelWidth();
        const char* sql = "SELECT query FROM history ORDER BY timestamp DESC LIMIT 8;";
        searchDB.stmt(sql).exec([&](std::string s){
          Button* item = searchMenu->addItem(s.c_str(), MapsApp::uiIcon("clock"), [=](){
            app->showPanel(searchPanel);
            queryText->setText(s.c_str());
            searchText(s, RETURN);
          });
          SvgPainter::elideText(static_cast<SvgText*>(item->selectFirst(".title")->node), uiWidth - 100);
        });
      }
    }
    return false;
  });

  Button* searchBtn = app->createPanelButton(MapsApp::uiIcon("search"), "Search", searchPanel);
  searchBtn->setMenu(searchMenu);
  return searchBtn;
}
