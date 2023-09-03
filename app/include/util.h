#pragma once

#include "ulib/stringutil.h"
//#include "ulib/threadutil.h"
#include "rapidjson/fwd.h"

#include "tangram.h"
#include "glm/vec2.hpp"

using Tangram::LngLat;
using Tangram::TileID;

namespace YAML { class Node; }

double lngLatDist(LngLat r1, LngLat r2);
double lngLatBearing(LngLat r1, LngLat r2);
TileID lngLatTile(LngLat ll, int z);
LngLat tileCoordToLngLat(const TileID& tileId, const glm::vec2& tileCoord);
LngLat parseLngLat(const char* s);

std::string yamlToStr(const YAML::Node& node);
template<typename T>
void yamlRemove(YAML::Node node, T key);
std::string osmIdFromProps(const rapidjson::Document& props);
std::string rapidjsonToStr(const rapidjson::Document& props);

struct sqlite3_stmt;
struct sqlite3;
typedef std::function<void(sqlite3_stmt*)> SQLiteStmtFn;
bool DB_exec(sqlite3* db, const char* sql, SQLiteStmtFn cb = SQLiteStmtFn(), SQLiteStmtFn bind = SQLiteStmtFn());

#include "isect2d.h"
#include "mapscomponent.h"

class MarkerGroup
{
public:
  using OnPickedFn = std::function<void()>;

  MarkerGroup(Map* _map, const std::string& _styling, const std::string _altStyling = "");
  ~MarkerGroup();
  void createMarker(LngLat pos, OnPickedFn cb, Properties&& props = {});
  void reset();
  void setVisible(bool vis);
  bool onPicked(MarkerID id);
  void onZoom();

  Map* map;
  std::string styling;
  std::string altStyling;
  bool visible = true;
  bool defaultVis = false;

  struct PlaceInfo {
    LngLat pos;
    Properties props;
    OnPickedFn callback;
    MarkerID markerId;
    MarkerID altMarkerId;
    bool isAltMarker;
  };
  std::vector<PlaceInfo> places;
  isect2d::ISect2D<glm::vec2> collider;
  float prevZoom = -1;

private:
  MarkerID getMarker(PlaceInfo& res);
};
