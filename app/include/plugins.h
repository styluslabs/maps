#pragma once

#include <list>
#include "mapscomponent.h"
//#include "js/JavaScript.h"
#include "duktape/duktape.h"

struct PluginFn
{
  std::string name;  //Tangram::JSFunctionIndex jsFnIdx;
  std::string type;
  std::string title;
};

class PluginManager : public MapsComponent
{
public:
  enum UrlReqType { NONE, SEARCH, PLACE, ROUTE } inState = NONE;

  PluginManager(MapsApp* _app);
  ~PluginManager();
  void reload();
  Button* createPanel();
  void onMapEvent(MapEvent_t event);
  void createFns(duk_context* ctx);
  std::string evalJS(const char* s);
  void cancelRequests(UrlReqType type);
  void notifyRequest(UrlRequestHandle handle, int serial);
  UrlReqType clearRequest(int serial);
  void jsSearch(int fnIdx, std::string queryStr, LngLat lngLat00, LngLat lngLat11, int flags);
  void jsPlaceInfo(int fnIdx, std::string props, Tangram::LngLat pos);
  void jsRoute(int fnIdx, std::string routeMode, const std::vector<LngLat>& waypts);
  static bool dukTryCall(duk_context* ctx, int nargs);

  template <typename... Types>
  void pushVars(std::string arg0, Types... args) { duk_push_string(jsContext, arg0.c_str()); pushVars(args...); }
  template <typename... Types>
  void pushVars(double arg0, Types... args) { duk_push_number(jsContext, arg0); pushVars(args...); }
  void pushVars() {}

  template <typename... Types>
  std::string jsCallFn(std::string fnname, Types... args)
  {
    std::string res;
    duk_context* ctx = jsContext;
    if(duk_get_global_string(ctx, fnname.c_str())) {
      pushVars(args...);
      if(dukTryCall(ctx, sizeof...(Types)))
        res = duk_safe_to_string(ctx, -1);
    }
    else
      LOGW("JS plugin function missing: %s", fnname.c_str());
    duk_pop(ctx);
    return res;
  }

  duk_context* jsContext = NULL;
  //std::mutex jsMutex;
  std::vector<PluginFn> searchFns;
  std::vector<PluginFn> routeFns;
  std::vector<PluginFn> placeFns;
  std::vector<PluginFn> commandFns;
  struct UrlRequest { UrlReqType type; UrlRequestHandle handle; int serial; };
  std::list<UrlRequest> pendingRequests;
  std::string onMapEventFn;

  static PluginManager* inst;
};
