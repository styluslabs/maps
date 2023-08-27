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
using Tangram::BinaryTileTask;
using Tangram::UrlRequestHandle;
using Tangram::MapProjection;

class MapsApp;

struct Location
{
  double time;
  double lat;
  double lng;
  float poserr;
  double alt;
  float alterr;
  float dir;
  float direrr;
  float spd;
  float spderr;
  //double dist;  // for tracks

  LngLat lngLat() const { return LngLat(lng, lat); }
};

class MapsComponent
{
public:
  MapsComponent(MapsApp* _app) : app(_app) {}
  MapsApp* app;

  void onMapChange() {}
};
