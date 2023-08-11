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
    DB_exec(searchDB, "CREATE TABLE history(query TEXT UNIQUE, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));");
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
        tileSrc->loadTileData(std::make_shared<Tangram::BinaryTileTask>(tileid, tileSrc), tilecb);
      }
    }
    //break; ???
  }

  return true;
}

void MapsSearch::clearSearchResults()
{
  app->pluginManager->cancelJsSearch();  // cancel any outstanding search requests
  mapResults.clear();
  listResults.clear();
  markers->reset();
}

void MapsSearch::clearSearch()
{
  clearSearchResults();
  queryText->setText("");
  if(app->searchActive) {
    app->map->updateGlobals({SceneUpdate{"global.search_active", "false"}});
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
  int offset = listResults.size();
  bool sortByDist = app->config["search"]["sort"].as<std::string>("rank") == "dist";
  // should we add tokenize = porter to CREATE TABLE? seems we want it on query, not content!
  std::string query = fstring("SELECT rowid, props, lng, lat, rank FROM points_fts WHERE points_fts "
      "MATCH ? ORDER BY osmSearchRank(%s, lng, lat) LIMIT 20 OFFSET ?;", sortByDist ? "-1" : "rank");
  DB_exec(searchDB, query.c_str(), [&](sqlite3_stmt* stmt){
    int rowid = sqlite3_column_int(stmt, 0);
    double lng = sqlite3_column_double(stmt, 2);
    double lat = sqlite3_column_double(stmt, 3);
    double score = sqlite3_column_double(stmt, 4);
    const char* json = (const char*)(sqlite3_column_text(stmt, 1));
    addListResult(rowid, lng, lat, score, json);
  }, [&](sqlite3_stmt* stmt){
    std::string s(queryStr + "*");
    std::replace(s.begin(), s.end(), '\'', ' ');
    sqlite3_bind_text(stmt, 1, s.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, offset);
  });
  moreListResultsAvail = listResults.size() - offset >= 20;
}

void MapsSearch::onMapChange()
{
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
      if(resultIdx <= 5 || lngLatDist(app->mapCenter.lngLat(), res.pos) < 2.0) {
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
  if(phase != NEXTPAGE) {
    searchStr = query;
    clearSearchResults();
    map->markerSetVisible(app->pickResultMarker, false);
    app->showPanel(searchPanel);
    app->gui->deleteContents(resultsContent, ".listitem");
    cancelBtn->setVisible(!searchStr.empty());
  }

  if(phase == EDITING) {
    std::vector<std::string> autocomplete;
    //std::string histq = fstring("SELECT query FROM history WHERE query LIKE '%s%%' ORDER BY timestamp LIMIT 5;", searchStr.c_str());
    std::string histq = "SELECT query FROM history WHERE query LIKE ? ORDER BY timestamp DESC LIMIT ?;";
    DB_exec(searchDB, histq.c_str(), [&](sqlite3_stmt* stmt){
      autocomplete.emplace_back( (const char*)(sqlite3_column_text(stmt, 0)) );
    }, [&](sqlite3_stmt* stmt){
      sqlite3_bind_text(stmt, 1, (searchStr + "%").c_str(), -1, SQLITE_TRANSIENT);
      // LIMIT 5 - leave room for results if running live search
      sqlite3_bind_int(stmt, 2, searchStr.size() > 2 && providerIdx == 0 ? 5 : 25);
    });
    populateAutocomplete(autocomplete);
  }

  if(searchStr.size() > 2) {
    // use map center for origin if current location is offscreen
    LngLat loc = app->currLocation.lngLat();
    searchOrigin = map->lngLatToScreenPosition(loc.longitude, loc.latitude) ? loc : app->mapCenter.lngLat();
    if(phase == RETURN) {
      DB_exec(searchDB, "INSERT OR REPLACE INTO history (query) VALUES (?);", NULL, [&](sqlite3_stmt* stmt){
        sqlite3_bind_text(stmt, 1, searchStr.c_str(), -1, SQLITE_TRANSIENT);
      });
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
      map->updateGlobals({SceneUpdate{"global.search_active", "true"}});
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
    Button* item = new Button(autoCompProto->clone());

    Button* discardBtn = createToolbutton(MapsApp::uiIcon("discard"), "Remove");
    discardBtn->onClicked = [=](){
      DB_exec(searchDB, "DELETE FROM history WHERE query = ?;", NULL, [&](sqlite3_stmt* stmt){
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
      });
      searchText(searchStr, EDITING);  // refresh
    };

    item->onClicked = [=](){
      queryText->setText(query.c_str());
      searchText(query, MapsSearch::RETURN);
    };
    SvgText* textnode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
    textnode->addText(history[ii].c_str());
    Widget* container = item->selectFirst(".child-container");
    container->addWidget(createStretch());
    container->addWidget(discardBtn);

    resultsContent->addWidget(item);
  }
}

void MapsSearch::populateResults(const std::vector<SearchResult>& results)
{
  for(size_t ii = 0; ii < results.size(); ++ii) {  //for(const auto& res : results)
    const SearchResult& res = results[ii];
    Button* item = new Button(searchResultProto->clone());
    item->onClicked = [this, &results, ii](){
      // TODO: hide search result marker
      app->setPickResult(results[ii].pos, results[ii].tags["name"].GetString(), results[ii].tags);
    };
    SvgText* titlenode = static_cast<SvgText*>(item->containerNode()->selectFirst(".title-text"));
    titlenode->addText(res.tags["name"].GetString());
    SvgText* distnode = static_cast<SvgText*>(item->containerNode()->selectFirst(".dist-text"));
    double distkm = lngLatDist(app->currLocation.lngLat(), res.pos);
    distnode->addText(fstring("%.1f km", distkm).c_str());
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
    searchText(s, MapsSearch::EDITING);  //StringRef(s).trimmed().toString();
  };

  queryText->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_RETURN) {
      if(!queryText->text().empty())
        searchText(queryText->text(), MapsSearch::RETURN);
      return true;
    }
    return false;
  });

  // this btn clears search text w/o closing panel (use back btn to close panel)
  cancelBtn = new Button(searchBoxNode->selectFirst(".cancel-btn"));
  cancelBtn->onClicked = [this](){
    clearSearch();
    searchText("", MapsSearch::EDITING);  // show history
  };
  cancelBtn->setVisible(false);

  auto searchTb = app->createPanelHeader(MapsApp::uiIcon("search"), "Search");
  if(!app->pluginManager->searchFns.empty()) {
    std::vector<std::string> cproviders = {"Offline"};
    for(auto& fn : app->pluginManager->searchFns)
      cproviders.push_back(fn.title.c_str());

    ComboBox* providerSel = createComboBox(cproviders);
    providerSel->onChanged = [=](const char*){
      providerIdx = providerSel->index();
    };
    searchTb->addWidget(providerSel);
  }

  // result sort order
  static const char* resultSortKeys[] = {"rank", "dist"};
  std::string initSort = app->config["search"]["sort"].as<std::string>("rank");
  size_t initSortIdx = 0;
  while(initSortIdx < 2 && initSort != resultSortKeys[initSortIdx]) ++initSortIdx;
  Menu* sortMenu = createRadioMenu({"Relevence", "Distance"}, [this](size_t ii){
    app->config["search"]["sort"] = resultSortKeys[ii];
    if(!queryText->text().empty())
      searchText(queryText->text(), MapsSearch::RETURN);
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
      searchText("", MapsSearch::EDITING);
    }
    else if(event->type == MapsApp::PANEL_CLOSED)
      clearSearch();
    return false;
  });

  auto scrollWidget = static_cast<ScrollWidget*>(resultsContent->parent());
  scrollWidget->onScroll = [=](){
    // get more list results
    if(moreListResultsAvail && scrollWidget->scrollY >= scrollWidget->scrollLimits.bottom)
      searchText(searchStr, NEXTPAGE);
  };

  static const char* searchResultProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g layout="flex" flex-direction="row" box-anchor="left">
        <g class="image-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/ui-icons.svg#search"/>
        </g>
        <g layout="box" box-anchor="vfill">
          <text class="title-text" box-anchor="left" margin="0 10"></text>
          <text class="addr-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
          <text class="dist-text weak" box-anchor="left bottom" margin="0 120" font-size="12"></text>
        </g>
      </g>
    </g>
  )";
  searchResultProto.reset(loadSVGFragment(searchResultProtoSVG));

  static const char* autoCompProtoSVG = R"(
    <g class="listitem" margin="0 5" layout="box" box-anchor="hfill">
      <rect box-anchor="fill" width="48" height="48"/>
      <g class="child-container" layout="flex" flex-direction="row" box-anchor="hfill">
        <g class="image-container" margin="2 5">
          <use class="icon" width="36" height="36" xlink:href=":/ui-icons.svg#clock"/>
        </g>
        <g layout="box" box-anchor="vfill">
          <text class="title-text" box-anchor="left" margin="0 10"></text>
          <text class="addr-text weak" box-anchor="left bottom" margin="0 10" font-size="12"></text>
        </g>
      </g>
    </g>
  )";
  autoCompProto.reset(loadSVGFragment(autoCompProtoSVG));

  markers.reset(new MarkerGroup(app->map, "layers.search-marker.draw.marker", "layers.search-dot.draw.marker"));

  initSearch();

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
          searchText(s, MapsSearch::RETURN);
        });
      });

    }
    return false;
  });

  Button* searchBtn = app->createPanelButton(MapsApp::uiIcon("search"), "Search", searchPanel);
  searchBtn->setMenu(searchMenu);
  return searchBtn;
}
