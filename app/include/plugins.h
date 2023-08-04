#pragma once

#include "mapscomponent.h"
//#include "js/JavaScript.h"
#include "duktape/duktape.h"

class Widget;

struct PluginFn
{
  std::string name;  //Tangram::JSFunctionIndex jsFnIdx;
  std::string title;
};

class PluginManager : public MapsComponent
{
public:
  PluginManager(MapsApp* _app, const std::string& pluginDir);
  ~PluginManager();
  void createFns(duk_context* ctx);
  std::string evalJS(const char* s);
  void cancelJsSearch();
  void cancelPlaceInfo();
  void jsSearch(int fnIdx, std::string queryStr, LngLat lngLat00, LngLat lngLat11, int flags);
  void jsPlaceInfo(int fnIdx, std::string id);

  Widget* createPanel();

  duk_context* jsContext;
  //std::mutex jsMutex;
  std::vector<PluginFn> searchFns;
  std::vector<PluginFn> placeFns;
  std::vector<PluginFn> commandFns;
  std::vector<UrlRequestHandle> searchRequests;
  std::vector<UrlRequestHandle> placeRequests;
  enum { NONE, SEARCH, PLACE } inState = NONE;

  static PluginManager* inst;
};
