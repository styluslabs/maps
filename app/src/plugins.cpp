#include "plugins.h"
#include "mapsapp.h"
#include "mapsearch.h"
#include "util.h"

using namespace Tangram;


PluginManager* PluginManager::inst = NULL;

const char* testJsSrc = R"#(

function nominatimSearch(query, bounds, flags)
{
  const url = "https://nominatim.openstreetmap.org/search?format=jsonv2&bounded=1&viewbox=" + bounds.join() + "&limit=50&q=" + query;
  jsonHttpRequest(url, function(content) {
    for(var ii = 0; ii < content.length; ii++) {
      const r = content[ii];
      const tags = {"name": r.display_name, [r.category]: r.type};
      const last = ii + 1 == content.length ? 4 : 0;
      addSearchResult(r.osm_id, r.lat, r.lon, r.importance, flags + last, tags);
    }
  });
}

registerFunction("nominatimSearch", "search", "Nominatim Search");

)#";

// how will plugins be defined, registered, tracked?
// yaml plugins:  section - in mapsources.yaml?  separate plugins.yaml?  plugins folder w/ js files?
// - run each js file for it to register fns

//static void dukErrorHander(void*, const char* message)
//{
//  LOGE("Plugins: fatal Duktape error: %s", message);
//}

static void dukTryCall(duk_context* ctx, int nargs)
{
  if(duk_pcall(ctx, nargs) != DUK_EXEC_SUCCESS) {
    if (duk_is_error(ctx, -1)) {
      duk_get_prop_string(ctx, -1, "stack");
      LOGW("JS call error: %s\n", duk_safe_to_string(ctx, -1));
      duk_pop(ctx);
    } else {
      LOGW("JS other error: %s\n", duk_safe_to_string(ctx, -1));
    }
  }
}

PluginManager::PluginManager(MapsApp* _app) : MapsComponent(_app)
{
  inst = this;
  jsContext = duk_create_heap_default();  //(NULL, NULL, NULL, NULL, dukErrorHander);
  loadPlugins(jsContext);
}

PluginManager::~PluginManager()
{
  inst = NULL;
  duk_destroy_heap(jsContext);
}

void PluginManager::jsSearch(int fnIdx, std::string queryStr, LngLat lngLat00, LngLat lngLat11, int flags)
{
  std::lock_guard<std::mutex> lock(jsMutex);
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
}

static int registerFunction(duk_context* ctx)
{
  // alternative is to pass fn object instead of name, which we can then add to globals w/ generated name
  std::string fntype = duk_require_string(ctx, 1);

  if(fntype == "search") {
    const char* name = duk_require_string(ctx, 0);
    const char* title = duk_require_string(ctx, 2);
    PluginManager::inst->searchFns.push_back({name, title});
  }
  else
    LOGE("Unsupported plugin function type %s", fntype.c_str());
  return 0;
}

static int jsonHttpRequest(duk_context* ctx)
{
  static int reqCounter = 0;
  // called from jsSearch, etc., so do not lock jsMutex (alternative is to use recursive_lock)
  const char* urlstr = duk_require_string(ctx, 0);
  auto url = Url(urlstr);
  std::string cbvar = fstring("_jsonHttpRequest_%d", reqCounter++);
  duk_dup(ctx, 1);
  duk_put_global_string(ctx, cbvar.c_str());
  //duk_push_global_stash(ctx);
  //duk_dup(ctx, 1);  // callback
  //duk_put_prop_string(ctx, -2, cbvar.c_str());
  MapsApp::platform->startUrlRequest(url, [ctx, cbvar, url](UrlResponse&& response) {
    if(response.error) {
      logMsg("Error fetching %s: %s\n", url.data().c_str(), response.error);
      return;
    }
    std::lock_guard<std::mutex> lock(PluginManager::inst->jsMutex);
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
    duk_json_decode(ctx, -1);
    dukTryCall(ctx, 1);
    duk_pop(ctx);
  });
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

  auto& ms = PluginManager::inst->app->mapsSearch;
  auto& res = flags & MapsSearch::MAP_SEARCH ? ms->addMapResult(osm_id, lng, lat, score)
                                             : ms->addListResult(osm_id, lng, lat, score);
  // duktape obj -> string -> rapidjson obj ... not ideal
  res.tags.Parse(duk_json_encode(ctx, 5));

  if(flags & 0x4)
    ms->createMarkers();

  return 0;
}

void PluginManager::loadPlugins(duk_context* ctx)
{
  // create C functions
  duk_push_c_function(ctx, registerFunction, 3);
  duk_put_global_string(ctx, "registerFunction");
  duk_push_c_function(ctx, jsonHttpRequest, 2);
  duk_put_global_string(ctx, "jsonHttpRequest");
  duk_push_c_function(ctx, addSearchResult, 2);
  duk_put_global_string(ctx, "addSearchResult");

  if(duk_pcompile_string(ctx, 0, testJsSrc) != 0)
    LOGW("JS compile error: %s\n%s\n---", duk_safe_to_string(ctx, -1), testJsSrc);
  else
    dukTryCall(ctx, 0);  // JS code should call registerFunction()
  duk_pop(ctx);
}
