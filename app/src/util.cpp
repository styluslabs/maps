#include "util.h"
#include "tangram.h"
#include "scene/scene.h"
#include "sqlite3/sqlite3.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"

#define PLATFORMUTIL_IMPLEMENTATION
#include "ulib/platformutil.h"

#define STRINGUTIL_IMPLEMENTATION
#include "ulib/stringutil.h"

#define FILEUTIL_IMPLEMENTATION
#include "ulib/fileutil.h"


template<typename T>
static constexpr T clamp(T val, T min, T max) {
    return val > max ? max : val < min ? min : val;
}

// https://stackoverflow.com/questions/27928
double lngLatDist(LngLat r1, LngLat r2)
{
  constexpr double p = 3.14159265358979323846/180;
  double a = 0.5 - cos((r2.latitude-r1.latitude)*p)/2 + cos(r1.latitude*p) * cos(r2.latitude*p) * (1-cos((r2.longitude-r1.longitude)*p))/2;
  return 12742 * asin(sqrt(a));  // kilometers
}

TileID lngLatTile(LngLat ll, int z)
{
  int x = int(floor((ll.longitude + 180.0) / 360.0 * (1 << z)));
  double latrad = ll.latitude * M_PI/180.0;
  int y = int(floor((1.0 - asinh(tan(latrad)) / M_PI) / 2.0 * (1 << z)));
  return TileID(x, y, z);
}

// segfault if GLM_FORCE_CTOR_INIT is defined for some units and not others!!!
#ifndef GLM_FORCE_CTOR_INIT
#error "GLM_FORCE_CTOR_INIT is not defined!"
#endif
LngLat tileCoordToLngLat(const TileID& tileId, const glm::vec2& tileCoord)
{
  using namespace Tangram;
  double scale = MapProjection::metersPerTileAtZoom(tileId.z);
  ProjectedMeters tileOrigin = MapProjection::tileSouthWestCorner(tileId);
  ProjectedMeters meters = glm::dvec2(tileCoord) * scale + tileOrigin;
  return MapProjection::projectedMetersToLngLat(meters);
}

std::string yamlToStr(const YAML::Node& node)
{
  YAML::Emitter emitter;
  emitter.SetSeqFormat(YAML::Flow);
  emitter.SetMapFormat(YAML::Flow);
  emitter << node;
  return std::string(emitter.c_str());
}

std::string osmIdFromProps(const rapidjson::Document& props)
{
  std::string osm_id;
  if(props.IsObject() && props.HasMember("osm_id") && props.HasMember("osm_type"))
    osm_id = props["osm_type"].GetString() + std::string(":") + props["osm_id"].GetString();
  return osm_id;
}

std::string rapidjsonToStr(const rapidjson::Document& props)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  props.Accept(writer);
  return sb.GetString();
}

// note that indices for sqlite3_column_* start from 0 while indices for sqlite3_bind_* start from 1
bool DB_exec(sqlite3* db, const char* sql, SQLiteStmtFn cb, SQLiteStmtFn bind)
{
  //if(sqlite3_exec(searchDB, sql, cb ? sqlite_static_helper : NULL, cb ? &cb : NULL, &zErrMsg) != SQLITE_OK) {
  auto t0 = std::chrono::high_resolution_clock::now();
  int res;
  sqlite3_stmt* stmt;
  if(sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
    LOGE("sqlite3_prepare_v2 error: %s", sqlite3_errmsg(db));
    return false;
  }
  if(bind)
    bind(stmt);
  while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {
    if(cb)
      cb(stmt);
  }
  if(res != SQLITE_DONE && res != SQLITE_OK)
    LOGE("sqlite3_step error: %s", sqlite3_errmsg(db));
  sqlite3_finalize(stmt);
  auto t1 = std::chrono::high_resolution_clock::now();
  //logMsg("Query time: %.6f s for %s\n", std::chrono::duration<float>(t1 - t0).count(), sql);
  return true;
}

// MarkerGroup
// - we may alter this is preserve markers when reset or to use ClientDataSource (at least for alt markers)

bool MarkerGroup::onPicked(MarkerID id)
{
  for(auto& res : places) {
    if(res.markerId == id) {
      res.callback();
      return true;
    }
  }
  return false;
}

MarkerID MarkerGroup::getMarker(PlaceInfo& res)
{
  MarkerID id = map->markerAdd();
  map->markerSetStylingFromPath(id, res.isAltMarker ? altStyling.c_str() : styling.c_str());
  map->markerSetVisible(id, visible);
  map->markerSetPoint(id, res.pos);
  map->markerSetProperties(id, Properties(res.props));
  map->markerSetDrawOrder(res.markerId, res.props.getNumber("priority") + (res.isAltMarker ? 0x10000 : 0));
  return id;
}

void MarkerGroup::setVisible(bool vis)
{
  visible = vis;
  for(auto& res : places)
    map->markerSetVisible(res.isAltMarker ? res.altMarkerId : res.markerId, vis);
  onZoom();
}


// if isect2d is insufficient, try https://github.com/nushoin/RTree - single-header r-tree impl
static isect2d::AABB<glm::vec2> markerAABB(Map* map, LngLat pos, double radius)
{
  double x, y;
  map->lngLatToScreenPosition(pos.longitude, pos.latitude, &x, &y);
  return isect2d::AABB<glm::vec2>(x - radius, y - radius, x + radius, y + radius);
}

MarkerGroup::MarkerGroup(Tangram::Map* _map, const std::string& _styling, const std::string _altStyling)
    : map(_map), styling(_styling), altStyling(_altStyling)
{
  int w = map->getViewportWidth(), h = map->getViewportHeight();
  collider.resize({16, 16}, {w, h});
  prevZoom = map->getZoom();
}

MarkerGroup::~MarkerGroup()
{
  reset();
}

void MarkerGroup::reset()
{
  for(auto& res : places) {
    map->markerRemove(res.markerId);
    map->markerRemove(res.altMarkerId);
  }
  places.clear();
  collider.clear();
  prevZoom = -1;
}

void MarkerGroup::createMarker(LngLat pos, OnPickedFn cb, Properties&& props)
{
  places.push_back({pos, props, cb, 0, 0, false});
  PlaceInfo& res = places.back();
  res.props.set("priority", places.size()-1);
  double markerRadius = map->getZoom() >= 17 ? 25 : 50;
  bool collided = false;
  collider.intersect(markerAABB(map, res.pos, markerRadius),
      [&](auto& a, auto& b) { collided = true; return false; });
  res.isAltMarker = collided;
  if(collided)
    res.altMarkerId = getMarker(res);
  else
    res.markerId = getMarker(res);
}

void MarkerGroup::onZoom()
{
  float zoom = map->getZoom();
  if(!visible || std::abs(zoom - prevZoom) < 0.5f) return;

  double markerRadius = zoom >= 17 ? 25 : 50;
  collider.clear();
  int w = map->getViewportWidth(), h = map->getViewportHeight();
  collider.resize({16, 16}, {w, h});
  if(zoom < prevZoom) {
    // if zoom decr by more than threshold, convert colliding pins to dots
    for(auto& res : places) {
      if(res.isAltMarker) continue;
      bool collided = false;
      collider.intersect(markerAABB(map, res.pos, markerRadius),
          [&](auto& a, auto& b) { collided = true; return false; });
      if(collided) {
        // convert to dot marker
        map->markerSetVisible(res.markerId, false);
        res.isAltMarker = true;
        if(res.altMarkerId <= 0)
          res.altMarkerId = getMarker(res);
        else
          map->markerSetVisible(res.altMarkerId, true);
      }
    }
  }
  else {
    // if zoom incr, convert dots to pins if no collision
    for(auto& res : places) {
      if(!res.isAltMarker)
        collider.insert(markerAABB(map, res.pos, markerRadius));
    }
    for(auto& res : places) {
      if(!res.isAltMarker) continue;
      // don't touch offscreen markers
      if(!map->lngLatToScreenPosition(res.pos.longitude, res.pos.latitude)) continue;
      bool collided = false;
      collider.intersect(markerAABB(map, res.pos, markerRadius),
          [&](auto& a, auto& b) { collided = true; return false; });
      if(!collided) {
        // convert to pin marker
        map->markerSetVisible(res.altMarkerId, false);
        res.isAltMarker = false;
        if(res.markerId <= 0)
          res.markerId = getMarker(res);
        else
          map->markerSetVisible(res.markerId, true);
      }
    }
  }
  prevZoom = zoom;
}
