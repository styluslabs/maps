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
  PluginManager(MapsApp* _app, const std::string& pluginDir);
  ~PluginManager();
  void createFns(duk_context* ctx);
  //void showGUI();
  void jsSearch(int fnIdx, std::string queryStr, LngLat lngLat00, LngLat lngLat11, int flags);

  duk_context* jsContext;
  std::mutex jsMutex;
  std::vector<PluginFn> searchFns;
  std::vector<PluginFn> commandFns;

  static PluginManager* inst;
};
