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
using Tangram::TileSource;
using Tangram::ClientDataSource;

class MapsApp;

// GUI classes
class Widget;
class Button;
class SvgNode;
class SvgDocument;
class SelectDialog;
class SelectBox;
class DragDropList;
class Toolbar;
class TextEdit;
class TextBox;
class Menu;
class Dialog;

enum MapEvent_t { MAP_CHANGE, LOC_UPDATE, MARKER_PICKED, SUSPEND, RESUME };

struct Location
{
  double time;  // seconds
  double lat;
  double lng;
  float poserr;  // meters
  double alt;  // meters
  float alterr;  // meters
  float dir;  // degrees
  float direrr;  // degrees
  float spd;  // m/s
  float spderr;  // m/s

  //Location(LngLat r, double _alt = 0, double t = 0) : time(t), lat(r.latitude), lng(r.longitude), alt(_alt) {}
  LngLat lngLat() const { return LngLat(lng, lat); }
};

class MapsComponent
{
public:
  MapsComponent(MapsApp* _app) : app(_app) {}
  MapsApp* app;

  //void onMapChange() {}  -- no point unless this is a virtual fn
};
