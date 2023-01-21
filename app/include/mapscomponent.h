#pragma once

#include "tangram.h"

//using namespace Tangram;
using Tangram::LngLat;
using Tangram::MarkerID;
using Tangram::SceneUpdate;
using Tangram::Map;
using Tangram::Platform;
using Tangram::Properties;
using Tangram::CameraPosition;
using Tangram::logMsg;
using Tangram::Url;
using Tangram::UrlResponse;
using Tangram::TileTaskCb;

class MapsApp;

class MapsComponent
{
public:
  MapsComponent(MapsApp* _app) : app(_app) {}
  MapsApp* app;

  void onMapChange() {}
};
