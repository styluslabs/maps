#include "plugins.h"
#include "mapsapp.h"
#include "mapsearch.h"
#include "mapsources.h"
#include "bookmarks.h"
#include "tracks.h"
#include "util.h"


PluginManager* PluginManager::inst = NULL;

// duktape ref: https://duktape.org/api.html

bool PluginManager::dukTryCall(duk_context* ctx, int nargs)
{
  if(duk_pcall(ctx, nargs) == DUK_EXEC_SUCCESS)
    return true;
  if(duk_is_error(ctx, -1)) {
    duk_get_prop_string(ctx, -1, "stack");
    LOGW("JS call error: %s\n", duk_safe_to_string(ctx, -1));
    duk_pop(ctx);
  }
  else
    LOGW("JS other error: %s\n", duk_safe_to_string(ctx, -1));
  return false;
}

PluginManager::PluginManager(MapsApp* _app, const std::string& pluginDir) : MapsComponent(_app)
{
  inst = this;
  jsContext = duk_create_heap_default();  //(NULL, NULL, NULL, NULL, dukErrorHander);
  createFns(jsContext);

  duk_context* ctx = jsContext;
  auto files = lsDirectory(pluginDir);
  for(auto& file : files) {
    if(file.substr(file.size() - 3) != ".js") continue;
    std::string js = readFile((pluginDir + "/" + file).c_str());
    duk_push_string(ctx, file.c_str());
    if(duk_pcompile_lstring_filename(ctx, 0, js.data(), js.size()) != 0)
      LOGW("JS compile error: %s\n%s\n---", duk_safe_to_string(ctx, -1), file.c_str());
    else
      dukTryCall(ctx, 0);  // JS code should call registerFunction()
    duk_pop(ctx);
  }
}

PluginManager::~PluginManager()
{
  inst = NULL;
  duk_destroy_heap(jsContext);
}

void PluginManager::cancelJsSearch()
{
  for(auto hnd : searchRequests)
    MapsApp::platform->cancelUrlRequest(hnd);  // no-op if request already completed
  searchRequests.clear();
}

void PluginManager::cancelPlaceInfo()
{
  for(auto hnd : placeRequests)
    MapsApp::platform->cancelUrlRequest(hnd);  // no-op if request already completed
  placeRequests.clear();
}

void PluginManager::jsSearch(int fnIdx, std::string queryStr, LngLat lngLat00, LngLat lngLat11, int flags)
{
  //std::lock_guard<std::mutex> lock(jsMutex);
  cancelJsSearch();
  inState = SEARCH;

  duk_context* ctx = jsContext;
  // fn
  duk_get_global_string(ctx, searchFns[fnIdx].name.c_str());
  // query
  duk_push_string(ctx, queryStr.c_str());
  // bounds
  auto arr_idx = duk_push_array(ctx);
  duk_push_number(ctx, lngLat00.longitude);
  duk_put_prop_index(ctx, arr_idx, 0);
  duk_push_number(ctx, lngLat00.latitude);
  duk_put_prop_index(ctx, arr_idx, 1);
  duk_push_number(ctx, lngLat11.longitude);
  duk_put_prop_index(ctx, arr_idx, 2);
  duk_push_number(ctx, lngLat11.latitude);
  duk_put_prop_index(ctx, arr_idx, 3);
  // flags
  duk_push_number(ctx, flags);
  // call the fn
  dukTryCall(ctx, 3);
  duk_pop(ctx);
  inState = NONE;
}

void PluginManager::jsPlaceInfo(int fnIdx, std::string id)
{
  cancelPlaceInfo();
  inState = PLACE;

  duk_context* ctx = jsContext;
  duk_get_global_string(ctx, placeFns[fnIdx].name.c_str());
  duk_push_string(ctx, id.c_str());
  // call the fn
  dukTryCall(ctx, 1);
  duk_pop(ctx);
  inState = NONE;
}

void PluginManager::jsRoute(int fnIdx, std::string routeMode, const std::vector<LngLat>& waypts)
{
  inState = ROUTE;

  duk_context* ctx = jsContext;

  duk_get_global_string(ctx, routeFns[fnIdx].name.c_str());

  duk_push_string(ctx, routeMode.c_str());

  auto array0 = duk_push_array(ctx);
  for(size_t ii = 0; ii < waypts.size(); ++ii) {
    auto array1 = duk_push_array(ctx);
    duk_push_number(ctx, waypts[ii].longitude);
    duk_put_prop_index(ctx, array1, 0);
    duk_push_number(ctx, waypts[ii].latitude);
    duk_put_prop_index(ctx, array1, 1);
    duk_put_prop_index(ctx, array0, ii);
  }
  dukTryCall(ctx, 1);
  duk_pop(ctx);
  inState = NONE;
}


std::string PluginManager::evalJS(const char* s)
{
  std::string result;
  duk_context* ctx = jsContext;
  if (duk_peval_string(ctx, s) != 0)
    result = fstring("JS eval error: %s", duk_safe_to_string(ctx, -1));
  else
    result = duk_safe_to_string(ctx, -1);
  duk_pop(ctx);
  return result;
}

static int registerFunction(duk_context* ctx)
{
  // alternative is to pass fn object instead of name, which we can then add to globals w/ generated name
  const char* name = duk_require_string(ctx, 0);
  std::string fntype = duk_require_string(ctx, 1);
  const char* title = duk_require_string(ctx, 2);

  if(fntype == "search")
    PluginManager::inst->searchFns.push_back({name, title});
  else if(fntype == "place")
    PluginManager::inst->placeFns.push_back({name, title});
  else if(fntype == "command")
    PluginManager::inst->commandFns.push_back({name, title});
  else
    LOGE("Unsupported plugin function type %s", fntype.c_str());
  return 0;
}

static void invokeHttpReqCallback(duk_context* ctx, std::string cbvar, const UrlResponse& response)
{
  // get the callback
  //duk_push_global_stash(ctx);
  //duk_get_prop_string(ctx, -2, cbvar.c_str());
  //duk_push_null(ctx);
  //duk_put_prop_string(ctx, -2, cbvar.c_str());  // release for GC
  duk_get_global_string(ctx, cbvar.c_str());
  duk_push_null(ctx);
  duk_put_global_string(ctx, cbvar.c_str());  // release for GC
  // parse response JSON and call callback
  duk_push_lstring(ctx, response.content.data(), response.content.size());
  char c0 = response.content.size() > 1 ? response.content[0] : '\0';
  // TODO: use DUK_USE_CPP_EXCEPTIONS to catch parsing errors!
  if(c0 == '[' || c0 == '{')
    duk_json_decode(ctx, -1);
  PluginManager::dukTryCall(ctx, 1);
  duk_pop(ctx);
}

static int jsonHttpRequest(duk_context* ctx)
{
  static int reqCounter = 0;
  // called from jsSearch, etc., so do not lock jsMutex (alternative is to use recursive_lock)
  const char* urlstr = duk_require_string(ctx, 0);
  const char* hdrstr = duk_require_string(ctx, 1);
  auto url = Url(urlstr);
  std::string cbvar = fstring("_jsonHttpRequest_%d", reqCounter++);
  duk_dup(ctx, 2);
  duk_put_global_string(ctx, cbvar.c_str());
  //duk_push_global_stash(ctx);
  //duk_dup(ctx, 1);  // callback
  //duk_put_prop_string(ctx, -2, cbvar.c_str());
  auto hnd = MapsApp::platform->startUrlRequest(url, hdrstr, [ctx, cbvar, url](UrlResponse&& response) {
    if(response.error)
      LOGE("Error fetching %s: %s\n", url.string().c_str(), response.error);
    else
      MapsApp::runOnMainThread([=](){ invokeHttpReqCallback(ctx, cbvar, response); });
    //std::lock_guard<std::mutex> lock(PluginManager::inst->jsMutex);
    //invokeHttpReqCallback(ctx, cbvar, response);
  });
  if(PluginManager::inst->inState == PluginManager::SEARCH)
    PluginManager::inst->searchRequests.push_back(hnd);
  else if(PluginManager::inst->inState == PluginManager::PLACE)
    PluginManager::inst->placeRequests.push_back(hnd);
  return 0;
}

static int addSearchResult(duk_context* ctx)
{
  // called from startUrlRequest callback so do not lock jsMutex
  // JS: addSearchResult(r.osm_id, r.lat, r.lon, r.importance, flags, tags);
  int64_t osm_id = duk_to_number(ctx, 0);
  double lat = duk_to_number(ctx, 1);
  double lng = duk_to_number(ctx, 2);
  double score = duk_to_number(ctx, 3);
  int flags = duk_to_number(ctx, 4);
  const char* json = duk_json_encode(ctx, 5);    // duktape obj -> string -> rapidjson obj ... not ideal

  //std::lock_guard<std::mutex> lock(PluginManager::inst->app->mapsSearch->resultsMutex);
  auto& ms = PluginManager::inst->app->mapsSearch;
  if(flags & MapsSearch::MAP_SEARCH) {
    ms->addMapResult(osm_id, lng, lat, score, json);
    ms->moreMapResultsAvail = flags & MapsSearch::MORE_RESULTS;
  }
  else {
    ms->addListResult(osm_id, lng, lat, score, json);
    ms->moreListResultsAvail = flags & MapsSearch::MORE_RESULTS;
  }
  return 0;
}

static int addMapSource(duk_context* ctx)
{
  const char* keystr = duk_require_string(ctx, 0);
  const char* yamlstr = duk_require_string(ctx, 1);
  try {
    PluginManager::inst->app->mapsSources->addSource(keystr, YAML::Load(yamlstr, strlen(yamlstr)));
  } catch (std::exception& e) {
    LOGE("Error parsing map source YAML: %s", e.what());
  }
  return 0;
}

static int addBookmark(duk_context* ctx)  //list, 0, props, note, lnglat[0], lnglat[1])
{
  const char* listname = duk_require_string(ctx, 0);
  const char* osm_id = duk_require_string(ctx, 1);
  const char* name = duk_require_string(ctx, 2);
  const char* props = duk_json_encode(ctx, 3);
  const char* notes = duk_require_string(ctx, 4);
  double lng = duk_to_number(ctx, 5);
  double lat = duk_to_number(ctx, 6);
  int date = duk_to_number(ctx, 7);

  auto& mb = PluginManager::inst->app->mapsBookmarks;
  int list_id = mb->getListId(listname, true);
  mb->addBookmark(list_id, osm_id, name, props, notes, LngLat(lng, lat), date);
  return 0;
}

static int addPlaceInfo(duk_context* ctx)
{
  const char* icon = duk_require_string(ctx, 0);
  const char* title = duk_require_string(ctx, 1);
  const char* value = duk_require_string(ctx, 2);
  PluginManager::inst->app->addPlaceInfo(icon, title, value);
  return 0;
}

static int addRouteGPX(duk_context* ctx)
{
  const char* gpx = duk_require_string(ctx, 0);
  std::unique_ptr<Track> track = MapsTracks::loadGPX(gpx);
  PluginManager::inst->app->mapsTracks->setRoute(std::move(track->route));
  return 0;
}

void PluginManager::createFns(duk_context* ctx)
{
  // create C functions
  duk_push_c_function(ctx, registerFunction, 3);
  duk_put_global_string(ctx, "registerFunction");
  duk_push_c_function(ctx, jsonHttpRequest, 3);
  duk_put_global_string(ctx, "jsonHttpRequest");
  duk_push_c_function(ctx, addSearchResult, 6);
  duk_put_global_string(ctx, "addSearchResult");
  duk_push_c_function(ctx, addMapSource, 2);
  duk_put_global_string(ctx, "addMapSource");
  duk_push_c_function(ctx, addBookmark, 8);
  duk_put_global_string(ctx, "addBookmark");
  duk_push_c_function(ctx, addPlaceInfo, 3);
  duk_put_global_string(ctx, "addPlaceInfo");
}

// commands should go in toolbar or menu of appropriate panel; e.g. bookmarks panel for import places plugin
// - so we need more detailed type than just "command"
/*void PluginManager::showGUI()
{
  if(!ImGui::CollapsingHeader("Plugin Commands", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  for(auto& cmd : commandFns) {
    if(ImGui::Button(cmd.title.c_str())) {
      std::lock_guard<std::mutex> lock(jsMutex);
      duk_context* ctx = jsContext;
      duk_get_global_string(ctx, cmd.name.c_str());
      dukTryCall(ctx, 0);
      duk_pop(ctx);
    }
  }
}*/

// for now, we need somewhere to put TextEdit for entering JS commands; probably would be opened from
//  overflow menu, assuming we even keep it

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "usvg/svgpainter.h"

#include "mapsources.h"

Button* PluginManager::createPanel()
{
  Widget* pluginContent = createColumn();
  pluginContent->node->setAttribute("box-anchor", "hfill");

  TextEdit* jsEdit = createTextEdit();
  jsEdit->node->setAttribute("box-anchor", "hfill");
  Button* runBtn = createPushbutton("Run");
  SvgText* resultTextNode = createTextNode("");
  TextBox* resultText = new TextBox(resultTextNode);

  runBtn->onClicked = [=](){
    resultText->setText( evalJS(jsEdit->text().c_str()).c_str() );
    resultText->setText( SvgPainter::breakText(resultTextNode, 300).c_str() );
  };

  pluginContent->addWidget(new TextBox(createTextNode("Javascript command:")));
  pluginContent->addWidget(jsEdit);
  pluginContent->addWidget(runBtn);
  pluginContent->addWidget(resultText);

  auto toolbar = app->createPanelHeader(MapsApp::uiIcon("textbox"), "Plugins");
  //toolbar->addWidget(createStretch());
  //toolbar->addWidget(maxZoomSpin);
  Widget* pluginPanel = app->createMapPanel(toolbar, NULL, pluginContent);

  Button* pluginBtn = app->createPanelButton(MapsApp::uiIcon("textbox"), "Plugin console", pluginPanel);
  return pluginBtn;
}
