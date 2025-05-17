#pragma once

#include "ulib/stringutil.h"
#include "ulib/fileutil.h"
#include "ulib/color.h"

#include "tangram.h"
#include "glm/vec2.hpp"

#define SQLITEPP_LOGW LOGW
#define SQLITEPP_LOGE LOGE
#include "sqlitepp.h"

// for MarkerGroup
#include "mapscomponent.h"

using Tangram::LngLat;
using Tangram::TileID;

namespace YAML { class Node; }

double lngLatDist(LngLat r1, LngLat r2);
double lngLatBearing(LngLat r1, LngLat r2);
TileID lngLatTile(LngLat ll, int z);
LngLat tileCoordToLngLat(const TileID& tileId, const glm::vec2& tileCoord);
std::pair<LngLat, LngLat> tileCoveringBounds(LngLat minLngLat, LngLat maxLngLat, int zoom);
LngLat parseLngLat(const char* s);
std::string lngLatToStr(LngLat ll);
int64_t packTileId(const TileID& tile);

// flowLevel = 0 to get flow YAML; flowLevel = 0 and indent = 0 to get JSON
std::string yamlToStr(const YAML::Node& node, int flowLevel = 0, int indent = 2);
template<typename T> void yamlRemove(YAML::Node& node, T key);
void saveListOrder(YAML::Node& node, const std::vector<std::string>& strs);
std::string osmIdFromJson(const YAML::Node& props);
//std::string rapidjsonToStr(const rapidjson::Document& props);
YAML::Node strToJson(const char* json);
YAML::Node strToJson(const std::string& json);
Tangram::Properties jsonToProps(const char* json);
Tangram::Properties jsonToProps(const std::string& json);
Tangram::Properties jsonToProps(const YAML::Node& tags);

std::string ftimestr(const char* fmt, int64_t msec_epoch = 0);
std::string colorToStr(const Color& c);

struct sqlite3_stmt;
struct sqlite3;
typedef std::function<void(sqlite3_stmt*)> SQLiteStmtFn;
bool DB_exec(sqlite3* db, const char* sql);

struct sqlite3_context;
struct sqlite3_value;
extern LngLat searchRankOrigin;
void udf_osmSearchRank(sqlite3_context* context, int argc, sqlite3_value** argv);

class MarkerGroup
{
public:
  using OnPickedFn = std::function<void()>;

  MarkerGroup(Map* _map, const std::string& _styling, const std::string _altStyling = "");
  ~MarkerGroup();
  void createMarker(LngLat pos, OnPickedFn cb, Properties&& props = {}, int id = -1);
  void reset();
  void setVisible(bool vis);
  bool onPicked(MarkerID id);
  void deleteMarker(int id);
  void updateMarker(int id, Properties&& props);

  Map* map;
  std::string styling;
  std::string altStyling;
  Properties commonProps;
  bool visible = true;
  bool defaultVis = false;

  struct PlaceInfo {
    int id;
    LngLat pos;
    Properties props;
    OnPickedFn callback;
    MarkerID markerId;
    MarkerID altMarkerId;
  };
  std::vector<PlaceInfo> places;

private:
  void createMapMarker(PlaceInfo& res);
};
