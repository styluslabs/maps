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

// initial bearing on great circle path from r1 to r2
double lngLatBearing(LngLat r1, LngLat r2)
{
  double y = sin(r2.longitude-r1.longitude) * cos(r2.latitude);
  double x = cos(r1.latitude)*sin(r2.latitude) - sin(r1.latitude)*cos(r2.latitude)*cos(r2.longitude-r1.longitude);
  return atan2(y, x);
}

TileID lngLatTile(LngLat ll, int z)
{
  int x = int(floor((ll.longitude + 180.0) / 360.0 * (1 << z)));
  double latrad = ll.latitude * M_PI/180.0;
  int y = int(floor((1.0 - asinh(tan(latrad)) / M_PI) / 2.0 * (1 << z)));
  return TileID(x, y, z);
}

static double parseCoord(const char* s, char** endptr)
{
  // strToReal will consume 'E' (for east), but not a problem unless followed by a digit (w/o space)
  char* p = (char*)s;
  double val = strToReal<double>(p, &p);
  if(p == s) val = double(NAN);
  while(isSpace(*p)) { ++p; }
  if(strncmp("°", p, 2) == 0) {
    char* q;
    p += 2;
    double tmp = strToReal<double>(p, &q);
    while(isSpace(*q)) { ++q; }
    if(*q == '\'') {
      val += tmp/60;
      p = ++q;
      tmp = strToReal<double>(p, &q);
      while(isSpace(*q)) { ++q; }
      if(*q == '\"' || (*q == '\'' && *++q == '\'')) {
        val += tmp/3600;
        p = ++q;
      }
    }
    while(isSpace(*p)) { ++p; }
  }
  bool ne = *p == 'n' || *p == 'N' || *p == 'e' || *p == 'E';
  bool sw = *p == 's' || *p == 'S' || *p == 'w' || *p == 'W';
  if(ne || sw) { ++p; while(isSpace(*p)) { ++p; } }
  if(endptr) *endptr = p;
  return sw ? -val : val;
}

// support z/lat/lng and lat,lng,zoom formats?
LngLat parseLngLat(const char* s)
{
  char* end;
  double lat = parseCoord(s, &end);
  if(*end == '/' || *end == ',' || *end == ';') { ++end; while(isSpace(*end)) { ++end; }  }
  double lng = parseCoord(end, &end);
  // if not at end of string, reject ... could be something like 38 N 1st St.
  return *end || lat < -90 || lat > 90 || lng < -180 || lng > 180 ? LngLat(NAN, NAN) : LngLat(lng, lat);
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

template<typename T>
void yamlRemove(YAML::Node node, T key)
{
  if(node.Type() != YAML::NodeType::Sequence)
    return;
  YAML::Node newnode = YAML::Node(YAML::NodeType::Sequence);
  for(YAML::Node& child : node) {
    if(child.as<T>() != key)
      newnode.push_back(child);
  }
  node = newnode;
}

// explicit instantiations
template void yamlRemove<int>(YAML::Node node, int key);

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

std::string ftimestr(const char* fmt, int64_t msec_epoch)
{
  char timestr[64];
  time_t t = msec_epoch <= 0 ? mSecSinceEpoch()/1000 : msec_epoch/1000;
  strftime(timestr, sizeof(timestr), fmt, localtime(&t));
  return std::string(timestr);
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

LngLat searchRankOrigin;

void udf_osmSearchRank(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  if(argc != 3) {
    sqlite3_result_error(context, "osmSearchRank - Invalid number of arguments (3 required).", -1);
    return;
  }
  if(sqlite3_value_type(argv[0]) != SQLITE_FLOAT || sqlite3_value_type(argv[1]) != SQLITE_FLOAT || sqlite3_value_type(argv[2]) != SQLITE_FLOAT) {
    sqlite3_result_double(context, -1.0);
    return;
  }
  // sqlite FTS5 rank is roughly -1*number_of_words_in_query; ordered from -\inf to 0
  double rank = /*sortByDist ? -1.0 :*/ sqlite3_value_double(argv[0]);
  double lon = sqlite3_value_double(argv[1]);
  double lat = sqlite3_value_double(argv[2]);
  double dist = lngLatDist(searchRankOrigin, LngLat(lon, lat));  // in kilometers
  // obviously will want a more sophisticated ranking calculation in the future
  sqlite3_result_double(context, rank/log2(1+dist));
}

// MarkerGroup
// - we may alter this to use ClientDataSource (at least for alt markers)

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

// icons and text are linked by set Label::setRelative() in PointStyleBuilder::addFeature()
// labels are collected and collided by LabelManager::updateLabelSet() - sorted by priority (lower number
//  is higher priority), collided, then sorted by order set by markerSetDrawOrder (not YAML "order") - higher
//  order means drawn later, i.e., on top
// note LabelCollider is only used when building the tile, while LabelManager is run for every frame
// blend_order only supported for style blocks: https://github.com/tangrams/tangram-es/issues/2039

MarkerID MarkerGroup::getMarker(PlaceInfo& res)
{
  MarkerID id = map->markerAdd();
  map->markerSetStylingFromPath(id, res.isAltMarker ? altStyling.c_str() : styling.c_str());
  map->markerSetVisible(id, visible);
  map->markerSetPoint(id, res.pos);
  map->markerSetProperties(id, Properties(res.props));
  map->markerSetDrawOrder(id, res.props.getNumber("priority") + (res.isAltMarker ? 0 : 0x10000));
  return id;
}

void MarkerGroup::setVisible(bool vis)
{
  if(visible == vis) return;
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

void MarkerGroup::updateMarker(int id, Properties&& props)
{
  for(auto it = places.begin(); it != places.end(); ++it) {
    if(it->id == id) {
      it->props = std::move(props);
      for(auto& item : commonProps.items())
        it->props.setValue(item.key, item.value);
      map->markerSetPoint(it->markerId, it->pos);  // to force marker update
      if(it->markerId > 0)
        map->markerSetProperties(it->markerId, Properties(it->props));
      if(it->altMarkerId > 0)
        map->markerSetProperties(it->altMarkerId, Properties(it->props));
      // we use fixed radius for marker, so changing title doesn't affect our collision calc
      break;
    }
  }
}

void MarkerGroup::deleteMarker(int id)
{
  for(auto it = places.begin(); it != places.end(); ++it) {
    if(it->id == id) {
      map->markerRemove(it->markerId);
      map->markerRemove(it->altMarkerId);
      places.erase(it);
      // marker deletion may allow nearby marker to be shown
      prevZoom = -1;
      onZoom();
      break;
    }
  }
}

void MarkerGroup::createMarker(LngLat pos, OnPickedFn cb, Properties&& props, int id)
{
  places.push_back({id, pos, std::move(props), cb, 0, 0, false});
  PlaceInfo& res = places.back();
  for(auto& item : commonProps.items())
    res.props.setValue(item.key, item.value);
  res.props.set("priority", places.size()-1);  //id < 0 ? places.size()-1 : id);
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
