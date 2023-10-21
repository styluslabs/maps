#include "mapsearch.h"
#include "mapsapp.h"
#include "bookmarks.h"
#include "plugins.h"
#include "resources.h"
#include "util.h"
#include "mapwidgets.h"

#include <deque>
#include "rapidjson/writer.h"
#include "data/tileData.h"
#include "data/formats/mvt.h"
#include "scene/scene.h"
#include "sqlite3/sqlite3.h"
//#include "isect2d.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"

// building search DB from tiles
static sqlite3* searchDB = NULL;
static sqlite3_stmt* insertStmt = NULL;

static LngLat searchOrigin;

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
  double rank = /*sortByDist ? -1.0 :*/ sqlite3_value_double(argv[0]);
  double lon = sqlite3_value_double(argv[1]);
  double lat = sqlite3_value_double(argv[2]);
  double dist = lngLatDist(searchOrigin, LngLat(lon, lat));  // in kilometers
  // obviously will want a more sophisticated ranking calculation in the future
  sqlite3_result_double(context, rank/log2(1+dist));
}

static void processTileData(TileTask* task, sqlite3_stmt* stmt, const std::vector<SearchData>& searchData)
{
  using namespace Tangram;
  // TODO: also support GeoJSON and TopoJSON for no source case
  auto tileData = task->source() ? task->source()->parse(*task) : Mvt::parseTile(*task, 0);
  for(const Layer& layer : tileData->layers) {
    for(const SearchData& searchdata : searchData) {
      if(searchdata.layer == layer.name) {
        for(const Feature& feature : layer.features) {
          std::string featname = feature.props.getString("name");
          if(featname.empty() || feature.points.empty())
            continue;  // skip POIs w/o name or geometry
          auto lnglat = tileCoordToLngLat(task->tileId(), feature.points.front());
          std::string tags;
          for(const std::string& field : searchdata.fields)
            tags.append(feature.props.getString(field)).append(" ");
          // insert row
          //sqlite3_bind_text(stmt, 1, featname.c_str(), -1, SQLITE_STATIC);
          sqlite3_bind_text(stmt, 1, tags.c_str(), tags.size() - 1, SQLITE_STATIC);  // drop trailing separator
          sqlite3_bind_text(stmt, 2, feature.props.toJson().c_str(), -1, SQLITE_TRANSIENT);
          sqlite3_bind_double(stmt, 3, lnglat.longitude);
          sqlite3_bind_double(stmt, 4, lnglat.latitude);
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
  /*int nchanges = sqlite3_total_changes(searchDB);
  const char* query = "INSERT OR IGNORE INTO tiles (z,x,y) VALUES (?,?,?);";
  DB_exec(searchDB, query, {}, [&](sqlite3_stmt* stmt){
    sqlite3_bind_int(stmt, 1, task->tileId().z);
    sqlite3_bind_int(stmt, 2, task->tileId().x);
    sqlite3_bind_int(stmt, 3, task->tileId().y);
  });
  if(sqlite3_total_changes(searchDB) == nchanges)  // total_changes() is a bit safer than changes() here
    return;
  // bind tile_id
  sqlite3_bind_int64(insertStmt, 6, sqlite3_last_insert_rowid(searchDB));*/

  sqlite3_exec(searchDB, "BEGIN TRANSACTION", NULL, NULL, NULL);
  processTileData(task, insertStmt, searchData);
  sqlite3_exec(searchDB, "COMMIT TRANSACTION", NULL, NULL, NULL);
}

std::vector<SearchData> MapsSearch::parseSearchFields(const YAML::Node& node)
{
  std::vector<SearchData> searchData;
  for(auto& elem : node)
    searchData.push_back({elem["layer"].Scalar(), splitStr<std::vector>(elem["fields"].Scalar(), ", ", true)});
  return searchData;
}

static const char* POI_SCHEMA = R"#(BEGIN;
CREATE TABLE tiles(id INTEGER PRIMARY KEY, z INTEGER, x INTEGER, y INTEGER, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));
CREATE UNIQUE INDEX tiles_tile_id ON tiles (z, x, y);
CREATE TABLE pois(name TEXT, tags TEXT, props TEXT, lng REAL, lat REAL, tile_id INTEGER);
CREATE VIRTUAL TABLE pois_fts USING fts5(name, tags, content='pois');
CREATE INDEX pois_tile_id ON pois (tile_id);

-- trigger to delete pois when tile row deleted
CREATE TRIGGER tiles_delete AFTER DELETE ON tiles BEGIN
  DELETE FROM pois WHERE tile_id = OLD.rowid;
END;

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

static bool initSearch()
{
  std::string dbPath = MapsApp::baseDir + "fts1.sqlite";
  if(sqlite3_open_v2(dbPath.c_str(), &searchDB, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
    sqlite3_close(searchDB);
    searchDB = NULL;

    ASSERT(0 && "Add support for name column!");

    // DB doesn't exist - create it
    if(sqlite3_open_v2(dbPath.c_str(), &searchDB, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
      logMsg("Error creating %s", dbPath.c_str());
      sqlite3_close(searchDB);
      searchDB = NULL;
      return false;
    }

    //CREATE VIRTUAL TABLE points_fts USING fts5(name, tags, props UNINDEXED, lng UNINDEXED, lat UNINDEXED);
    DB_exec(searchDB, POI_SCHEMA);
    // search history - NOCASE causes comparisions to be case-insensitive but still stores case
    DB_exec(searchDB, "CREATE TABLE history(query TEXT UNIQUE COLLATE NOCASE, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));");
  }
  //sqlite3_exec(searchDB, "PRAGMA synchronous=OFF", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA count_changes=OFF", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA journal_mode=MEMORY", NULL, NULL, &errorMessage);
  //sqlite3_exec(searchDB, "PRAGMA temp_store=MEMORY", NULL, NULL, &errorMessage);

  char const* stmtStr = "INSERT INTO points_fts (tags,props,lng,lat) VALUES (?,?,?,?);";
  //char const* stmtStr = "INSERT INTO pois (name,tags,props,lng,lat,tile_id) VALUES (?,?,?,?,?,?);";
  if(sqlite3_prepare_v2(searchDB, stmtStr, -1, &insertStmt, NULL) != SQLITE_OK) {
    logMsg("sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(searchDB));
    return false;
  }

  if(sqlite3_create_function(searchDB, "osmSearchRank", 3, SQLITE_UTF8, 0, udf_osmSearchRank, 0, 0) != SQLITE_OK)
    logMsg("sqlite3_create_function: error creating osmSearchRank");
  return true;
}

MapsSearch::MapsSearch(MapsApp* _app) : MapsComponent(_app)
{
  initSearch();
}

bool MapsSearch::indexMBTiles()
{
  Map* map = app->map;
  YAML::Node searchDataNode;
  Tangram::YamlPath("global.search_data").get(map->getScene()->config(), searchDataNode);
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

void MapsSearch::clearSearchResults()
{
  app->pluginManager->cancelRequests(PluginManager::SEARCH);  // cancel any outstanding search requests
  app->gui->deleteContents(resultsContent, ".listitem");
  mapResults.clear();
  listResults.clear();
  markers->reset();
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
  size_t idx = mapResults.size();
  mapResults.push_back({id, {lng, lat}, rank, {}});
  rapidjson::Document& tags = mapResults.back().tags;
  tags.Parse(json);
  if(!tags.IsObject() || !tags.HasMember("name")) {
    mapResults.pop_back();
    return;
  }
  Properties props;
  //props.set("priority", idx);
  for(auto& m : mapResults.back().tags.GetObject()) {
    if(m.value.IsNumber())
      props.set(m.name.GetString(), m.value.GetDouble());
    else if(m.value.IsString())
      props.set(m.name.GetString(), m.value.GetString());
  }
  auto onPicked = [this, idx](){
    SearchResult& res = mapResults[idx];
    app->setPickResult(res.pos, res.tags["name"].GetString(), res.tags);
  };
  markers->createMarker({lng, lat}, onPicked, std::move(props));
  mapResultsChanged = true;
}

void MapsSearch::addListResult(int64_t id, double lng, double lat, float rank, const char* json)
{
  listResults.push_back({id, {lng, lat}, rank, {}});
  rapidjson::Document& tags = listResults.back().tags;
  tags.Parse(json);
  if(!tags.IsObject() || !tags.HasMember("name"))
    mapResults.pop_back();
  //return listResults.back();
}

void MapsSearch::searchPluginError(const char* err)
{
  retryBtn->setVisible(true);
}

// online map search C++ code removed 2022-12-11 (now handled via plugins)

void MapsSearch::offlineMapSearch(std::string queryStr, LngLat lnglat00, LngLat lngLat11)
{
  const char* query = "SELECT rowid, props, lng, lat, rank FROM points_fts WHERE points_fts "
      "MATCH ? AND lng >= ? AND lat >= ? AND lng <= ? AND lat <= ? ORDER BY rank LIMIT 1000;";
  DB_exec(searchDB, query, [&](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    double lng = sqlite3_column_double(stmt, 2);
    double lat = sqlite3_column_double(stmt, 3);
    double score = sqlite3_column_double(stmt, 4);
    const char* json = (const char*)(sqlite3_column_text(stmt, 1));
    addMapResult(rowid, lng, lat, score, json);
  }, [&](sqlite3_stmt* stmt){
    sqlite3_bind_text(stmt, 1, queryStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, lnglat00.longitude);
    sqlite3_bind_double(stmt, 3, lnglat00.latitude);
    sqlite3_bind_double(stmt, 4, lngLat11.longitude);
    sqlite3_bind_double(stmt, 5, lngLat11.latitude);
  });
  moreMapResultsAvail = mapResults.size() >= 1000;
}

void MapsSearch::offlineListSearch(std::string queryStr, LngLat, LngLat)
{
  int offset = listResults.size();
  // if '*' not appended to string, we assume catagorical search - no info for ranking besides dist
  bool sortByDist = queryStr.back() != '*' || app->config["search"]["sort"].as<std::string>("rank") == "dist";
  // should we add tokenize = porter to CREATE TABLE? seems we want it on query, not content!
  std::string query = fstring("SELECT rowid, props, lng, lat, rank FROM points_fts WHERE points_fts "
      "MATCH ? ORDER BY osmSearchRank(%s, lng, lat) LIMIT 20 OFFSET ?;", sortByDist ? "-1.0" : "rank");
  DB_exec(searchDB, query.c_str(), [&](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    double lng = sqlite3_column_double(stmt, 2);
    double lat = sqlite3_column_double(stmt, 3);
    double score = sqlite3_column_double(stmt, 4);
    const char* json = (const char*)(sqlite3_column_text(stmt, 1));
    addListResult(rowid, lng, lat, score, json);
  }, [&](sqlite3_stmt* stmt){
    sqlite3_bind_text(stmt, 1, queryStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, offset);
  });
  moreListResultsAvail = listResults.size() - offset >= 20;
}

void MapsSearch::onMapEvent(MapEvent_t event)
{
  if(event != MAP_CHANGE)
    return;
  Map* map = app->map;
  LngLat lngLat00, lngLat11;
  app->getMapBounds(lngLat00, lngLat11);
  if(app->searchActive && ((moreMapResultsAvail && map->getZoom() - prevZoom > 0.5f)
      || lngLat00.longitude < dotBounds00.longitude || lngLat00.latitude < dotBounds00.latitude
      || lngLat11.longitude > dotBounds11.longitude || lngLat11.latitude > dotBounds11.latitude)) {
    updateMapResults(lngLat00, lngLat11);
    prevZoom = map->getZoom();
  }
  else if(!mapResults.empty() && std::abs(map->getZoom() - prevZoom) > 0.5f) {
    markers->onZoom();
  }

  if(app->pickedMarkerId > 0) {
    if(markers->onPicked(app->pickedMarkerId))
      app->pickedMarkerId = 0;
  }
}

void MapsSearch::updateMapResults(LngLat lngLat00, LngLat lngLat11)
{
  double lng01 = fabs(lngLat11.longitude - lngLat00.longitude);
  double lat01 = fabs(lngLat11.latitude - lngLat00.latitude);
  dotBounds00 = LngLat(lngLat00.longitude - lng01/8, lngLat00.latitude - lat01/8);
  dotBounds11 = LngLat(lngLat11.longitude + lng01/8, lngLat11.latitude + lat01/8);
  // should we do clearJsSearch() to prevent duplicate results?
  mapResults.clear();
  markers->reset();
  if(providerIdx > 0)
    app->pluginManager->jsSearch(providerIdx - 1, searchStr, dotBounds00, dotBounds11, MAP_SEARCH);
  else
    offlineMapSearch(searchStr, dotBounds00, dotBounds11);
}

void MapsSearch::resultsUpdated()
{
  populateResults(listResults);

  // zoom out if necessary to show first 5 results
  if(mapResultsChanged) {
    Map* map = app->map;
    //createMarkers();

    LngLat minLngLat(180, 90), maxLngLat(-180, -90);
    int resultIdx = 0;
    for(auto& res : listResults) {
      if(resultIdx <= 5 || lngLatDist(app->getMapCenter(), res.pos) < 2.0) {
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
        auto pos = map->getEnclosingCameraPosition(minLngLat, maxLngLat, {32});
        pos.zoom = std::min(pos.zoom, 16.0f);
        map->flyTo(pos, 1.0);
      }
    }
  }
}

void MapsSearch::searchText(std::string query, SearchPhase phase)
{
  Map* map = app->map;
  LngLat lngLat00, lngLat11;
  app->getMapBounds(lngLat00, lngLat11);
  query = StringRef(query).trimmed().toString();
  if(phase != NEXTPAGE) {
    // add synonyms to query (e.g., add "fast food" to "restaurant" query)
    if(providerIdx == 0 && !query.empty()) {
      // jsCallFn will return empty string in case of error
      std::string tfquery = app->pluginManager->jsCallFn("transformQuery", query);
      searchStr = tfquery.empty() ? query + "*" : tfquery;
      std::replace(searchStr.begin(), searchStr.end(), '\'', ' ');
      LOG("Search string: %s", searchStr.c_str());
    }
    else
      searchStr = query;
    clearSearchResults();
    map->markerSetVisible(app->pickResultMarker, false);
    app->showPanel(searchPanel);
    cancelBtn->setVisible(!searchStr.empty());
  }
  retryBtn->setVisible(false);

  if(phase == EDITING) {
    std::vector<std::string> autocomplete;
    std::string histq = "SELECT query FROM history WHERE query LIKE ? ORDER BY timestamp DESC LIMIT ?;";
    DB_exec(searchDB, histq.c_str(), [&](sqlite3_stmt* stmt){
      autocomplete.emplace_back( (const char*)(sqlite3_column_text(stmt, 0)) );
    }, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, (query + "%").c_str(), -1, SQLITE_TRANSIENT);
      // LIMIT 5 - leave room for results if running live search
      sqlite3_bind_int(stmt, 2, query.size() > 2 && providerIdx == 0 ? 5 : 25);
    });
    populateAutocomplete(autocomplete);
  }

  if(query.size() > 2 || phase == NEXTPAGE) {
    // use map center for origin if current location is offscreen
    if(phase != NEXTPAGE) {
      LngLat loc = app->currLocation.lngLat();
      searchOrigin = map->lngLatToScreenPosition(loc.longitude, loc.latitude) ? loc : app->getMapCenter();
    }
    if(phase == RETURN) {
      DB_exec(searchDB, "INSERT OR REPLACE INTO history (query) VALUES (?);", NULL, [&](sqlite3_stmt* stmt){
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
      });
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
      updateMapResults(lngLat00, lngLat11);
    }

    if(providerIdx == 0) {
      offlineListSearch(searchStr, lngLat00, lngLat11);
      resultsUpdated();
    }
    else if(phase != EDITING) {
      bool sortByDist = app->config["search"]["sort"].as<std::string>("rank") == "dist";
      app->pluginManager->jsSearch(providerIdx - 1, searchStr, lngLat00, lngLat11, sortByDist ? SORT_BY_DIST : 0);
    }

    if(!app->searchActive && phase == RETURN) {
      //map->updateGlobals({SceneUpdate{"global.search_active", "true"}});
      map->getScene()->hideExtraLabels = true;
      app->mapsBookmarks->hideBookmarks();  // also hide tracks?
      app->searchActive = true;
    }
  }
}

// or should we put results in resultList and show that immediately?
void MapsSearch::populateAutocomplete(const std::vector<std::string>& history)
{
  //autoCompContainer->setVisible(true);
  //window()->gui()->deleteContents(autoCompList, ".listitem");

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
      DB_exec(searchDB, "DELETE FROM history WHERE query = ?;", NULL, [&](sqlite3_stmt* stmt){
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
      });
      searchText(queryText->text(), EDITING);  // refresh
    };
    container->addWidget(discardBtn);

    resultsContent->addWidget(item);
  }
}

void MapsSearch::populateResults(const std::vector<SearchResult>& results)
{
  for(size_t ii = 0; ii < results.size(); ++ii) {  //for(const auto& res : results)
    const SearchResult& res = results[ii];
    std::string placetype = MapsApp::osmPlaceType(res.tags);
    Button* item = createListItem(MapsApp::uiIcon("search"), res.tags["name"].GetString(), placetype.c_str());
    item->onClicked = [this, &results, ii](){
      app->setPickResult(results[ii].pos, results[ii].tags["name"].GetString(), results[ii].tags);
    };
    double distkm = lngLatDist(app->currLocation.lngLat(), res.pos);
    TextBox* distText = new TextBox(createTextNode(fstring("%.1f km", distkm).c_str()));
    distText->node->addClass("weak");
    distText->node->setAttribute("font-size", "12");
    item->selectFirst(".child-container")->addWidget(distText);
    resultsContent->addWidget(item);
  }
}

Button* MapsSearch::createPanel()
{
  static const char* searchBoxSVG = R"#(
    <g id="searchbox" class="inputbox toolbar" box-anchor="hfill" layout="box">
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
          <use class="icon" width="30" height="30" xlink:href=":/ui-icons.svg#circle-x"/>
        </g>
      </g>
    </g>
  )#";

  SvgG* searchBoxNode = static_cast<SvgG*>(loadSVGFragment(searchBoxSVG));
  SvgG* textEditNode = static_cast<SvgG*>(searchBoxNode->selectFirst(".textbox"));
  textEditNode->addChild(textEditInnerNode());
  queryText = new TextEdit(textEditNode);
  setMinWidth(queryText, 100);

  queryText->onChanged = [this](const char* s){
    searchText(s, EDITING);
  };

  queryText->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_RETURN) {
      if(!queryText->text().empty())
        searchText(queryText->text(), RETURN);
      return true;
    }
    return false;
  });

  // this btn clears search text w/o closing panel (use back btn to close panel)
  cancelBtn = new Button(searchBoxNode->selectFirst(".cancel-btn"));
  cancelBtn->onClicked = [this](){
    clearSearch();
    searchText("", EDITING);  // show history
  };

  retryBtn = new Button(searchBoxNode->selectFirst(".retry-btn"));
  retryBtn->onClicked = [this](){
    if(!queryText->text().empty())
      searchText(queryText->text(), RETURN);
  };

  providerIdx = app->config["search"]["plugin"].as<int>(0);
  std::vector<std::string> cproviders = {"Offline Search"};
  for(auto& fn : app->pluginManager->searchFns)
    cproviders.push_back(fn.title.c_str());

  auto searchTb = app->createPanelHeader(MapsApp::uiIcon("search"), cproviders[providerIdx].c_str());
  bool hasPlugins = !app->pluginManager->searchFns.empty();
  Button* searchPluginBtn = createToolbutton(MapsApp::uiIcon(hasPlugins ? "plugin" : "no-plugin"), "Plugin");
  searchPluginBtn->setEnabled(hasPlugins);
  Menu* searchPluginMenu = createMenu(Menu::VERT_LEFT, false);
  for(size_t ii = 0; ii < cproviders.size(); ++ii) {
    std::string title = cproviders[ii];
    searchPluginMenu->addItem(title.c_str(), [=](){
      searchPanel->selectFirst(".panel-title")->setText(title.c_str());
      app->config["search"]["plugin"] = providerIdx = ii;
    });
  }
  searchPluginBtn->setMenu(searchPluginMenu);
  searchTb->addWidget(searchPluginBtn);

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

  //Widget* searchContent = createColumn(); //createListContainer();
  resultsContent = createColumn(); //createListContainer();
  //searchContent->addWidget(new Widget(searchBoxNode));
  //searchContent->addWidget(resultsContent);
  searchPanel = app->createMapPanel(searchTb, resultsContent, new Widget(searchBoxNode));

  searchPanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == MapsApp::PANEL_OPENED) {
      app->gui->setFocused(queryText);
      // show history
      searchText("", EDITING);
    }
    else if(event->type == MapsApp::PANEL_CLOSED)
      clearSearch();
    return false;
  });

  auto scrollWidget = static_cast<ScrollWidget*>(resultsContent->parent());
  scrollWidget->onScroll = [=](){
    // get more list results
    if(moreListResultsAvail && scrollWidget->scrollY >= scrollWidget->scrollLimits.bottom)
      searchText("", NEXTPAGE);
  };

  markers.reset(new MarkerGroup(app->map, "layers.search-marker.draw.marker", "layers.search-dot.draw.marker"));

  // main toolbar button
  Menu* searchMenu = createMenu(Menu::VERT_LEFT);
  //searchMenu->autoClose = true;
  searchMenu->addHandler([this, searchMenu](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::VISIBLE) {
      gui->deleteContents(searchMenu->selectFirst(".child-container"));

      // TODO: pinned searches - timestamp column = INF?
      DB_exec(searchDB, "SELECT query FROM history ORDER BY timestamp DESC LIMIT 8;", [&](sqlite3_stmt* stmt){
        std::string s = (const char*)(sqlite3_column_text(stmt, 0));
        searchMenu->addItem(s.c_str(), MapsApp::uiIcon("clock"), [=](){
          app->showPanel(searchPanel);
          queryText->setText(s.c_str());
          searchText(s, RETURN);
        });
      });

    }
    return false;
  });

  Button* searchBtn = app->createPanelButton(MapsApp::uiIcon("search"), "Search", searchPanel);
  searchBtn->setMenu(searchMenu);
  return searchBtn;
}
