#pragma once

#include "mapscomponent.h"
//#include "js/JavaScript.h"
#include "duktape/duktape.h"

struct PluginFn
{
  std::string name;  //Tangram::JSFunctionIndex jsFnIdx;
  std::string title;
};

class PluginManager : public MapsComponent
{
public:
  PluginManager(MapsApp* _app);
  ~PluginManager();
  void loadPlugins(duk_context* ctx);

  void jsSearch(int fnIdx, std::string queryStr, LngLat lngLat00, LngLat lngLat11, int flags);

  std::mutex jsMutex;

  std::vector<PluginFn> searchFns;

  duk_context* jsContext;

  static PluginManager* inst;
};
