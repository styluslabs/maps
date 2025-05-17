#include "util.h"
#include "tangram.h"
#include "scene/scene.h"
#include "sqlite3/sqlite3.h"
#include "usvg/svgwriter.h"


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
  double dlng = M_PI*(r2.longitude-r1.longitude)/180;
  double lat1 = M_PI*r1.latitude/180, lat2 = M_PI*r2.latitude/180;
  double y = sin(dlng) * cos(lat2);
  double x = cos(lat1)*sin(lat2) - sin(lat1)*cos(lat2)*cos(dlng);
  return atan2(y, x);
}

TileID lngLatTile(LngLat ll, int z)
{
  int x = int(floor((ll.longitude + 180.0) / 360.0 * (1 << z)));
  double latrad = ll.latitude * M_PI/180.0;
  int y = int(floor((1.0 - asinh(tan(latrad)) / M_PI) / 2.0 * (1 << z)));
  return TileID(x, y, z);
}

int64_t packTileId(const TileID& tile)
{
  // we could interleave bits of x and y to get something like quadkey (but note we still need to include z)
  return int64_t(tile.z) << 48 | int64_t(tile.x) << 24 | int64_t(tile.y);
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

std::string lngLatToStr(LngLat ll)
{
  return fstring("%.6f, %.6f", ll.latitude, ll.longitude);
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

static glm::dvec2 projMetersToTileCoords(Tangram::ProjectedMeters meters, int zoom)
{
  double hc = MapProjection::EARTH_HALF_CIRCUMFERENCE_METERS;
  double invTileSize = (1 << zoom) / MapProjection::EARTH_CIRCUMFERENCE_METERS;
  glm::dvec2 tileSpaceOrigin(-hc, hc);
  glm::dvec2 tileSpaceAxes(invTileSize, -invTileSize);
  return (meters - tileSpaceOrigin) * tileSpaceAxes;
}

// expand bbox {minLngLat, maxLngLat} to tile boundaries at specified zoom (used for Tilemaker)
std::pair<LngLat, LngLat> tileCoveringBounds(LngLat minLngLat, LngLat maxLngLat, int zoom)
{
  auto minMeters = MapProjection::lngLatToProjectedMeters(minLngLat);
  auto maxMeters = MapProjection::lngLatToProjectedMeters(maxLngLat);
  auto minTile = glm::floor(projMetersToTileCoords(minMeters, zoom));
  auto maxTile = glm::floor(projMetersToTileCoords(maxMeters, zoom));
  double eps = 0.1/MapProjection::metersPerTileAtZoom(zoom);
  LngLat minBBox = tileCoordToLngLat(TileID(minTile.x, minTile.y, zoom), {eps, eps});
  LngLat maxBBox = tileCoordToLngLat(TileID(maxTile.x, maxTile.y, zoom), {1-eps, 1-eps});
  return {minBBox, maxBBox};
}

std::string yamlToStr(const YAML::Node& node, int flowLevel, int indent)
{
  YAML::Writer emitter;
  emitter.flowLevel = flowLevel;
  emitter.indent = indent;
  return emitter.convert(node);
}

template<typename T>
void yamlRemove(YAML::Node& node, T key)
{
  if(node.Type() != YAML::NodeType::Sequence)
    return;
  YAML::Node newnode(node.getFlags());
  for(auto& child : node) {
    if(child.as<T>() != key)
      newnode.push_back(std::move(child));
  }
  node = std::move(newnode);  //node;
}

// explicit instantiations
template void yamlRemove<int>(YAML::Node& node, int key);

// key point here is that we don't overwrite node with empty list
void saveListOrder(YAML::Node& node, const std::vector<std::string>& strs)
{
  if(strs.empty()) { return; }
  node = YAML::Node(YAML::Tag::ARRAY | YAML::Tag::YAML_FLOW);
  for(const auto& str : strs) {
    node.push_back(str);
  }
}

std::string osmIdFromJson(const YAML::Node& props)
{
  if(props.IsMap()) {
    const auto& id = props["osm_id"];
    const auto& type = props["osm_type"];
    if(id && type) {
      std::string idstr = id.isNumber() ? std::to_string(int64_t(id.getNumber())) : id.getString();
      return type.getString() + ":" + idstr;
    }
    if(const auto& wiki = props["wiki"])
      return "wiki:" + wiki.getString();
  }
  return {};
}

YAML::Node strToJson(const char* json) { return YAML::parse(json, 0, YAML::PARSE_JSON); }
YAML::Node strToJson(const std::string& json) { return YAML::parse(json, YAML::PARSE_JSON); }

Tangram::Properties jsonToProps(const char* json) { return jsonToProps(strToJson(json)); }
Tangram::Properties jsonToProps(const std::string& json) { return jsonToProps(strToJson(json.c_str())); }

Properties jsonToProps(const YAML::Node& tags)
{
  Properties props;
  if(!tags.IsMap()) return props;
  for(auto m : tags.pairs()) {
    if(m.second.isNumber())
      props.set(m.first.getString(), m.second.getNumber());
    else if(m.second.isString())
      props.set(m.first.getString(), m.second.getString());
  }
  return props;
}

std::string ftimestr(const char* fmt, int64_t msec_epoch)
{
  char timestr[64];
  time_t t = msec_epoch <= 0 ? mSecSinceEpoch()/1000 : msec_epoch/1000;
  strftime(timestr, sizeof(timestr), fmt, localtime(&t));
  return std::string(timestr);
}

std::string colorToStr(const Color& c)
{
  char buff[64];
  SvgWriter::serializeColor(buff, c);
  return std::string(buff);
}

bool DB_exec(sqlite3* db, const char* sql)
{
  if(sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
    LOGE("sqlite3_exec error: %s", sqlite3_errmsg(db));
    return false;
  }
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

void MarkerGroup::createMapMarker(PlaceInfo& res)
{
  MarkerID id = map->markerAdd();
  map->markerSetStylingFromPath(id, styling.c_str());
  map->markerSetVisible(id, visible);
  map->markerSetPoint(id, res.pos);
  map->markerSetProperties(id, Properties(res.props));
  map->markerSetDrawOrder(id, res.props.getNumber("priority") + 0x10000);

  if(!altStyling.empty()) {
    MarkerID altid = map->markerAdd();
    map->markerSetStylingFromPath(altid, altStyling.c_str());
    map->markerSetVisible(altid, visible);
    map->markerSetPoint(altid, res.pos);
    map->markerSetProperties(altid, Properties(res.props));
    map->markerSetDrawOrder(altid, res.props.getNumber("priority"));
    map->markerSetAlternate(id, altid);
    res.altMarkerId = altid;
  }
  res.markerId = id;
}

void MarkerGroup::setVisible(bool vis)
{
  if(visible == vis) return;
  visible = vis;
  for(auto& res : places) {
    map->markerSetVisible(res.markerId, vis);  //res.isAltMarker ? res.altMarkerId
    map->markerSetVisible(res.altMarkerId, vis);  //res.isAltMarker ? res.altMarkerId
  }
}

MarkerGroup::MarkerGroup(Tangram::Map* _map, const std::string& _styling, const std::string _altStyling)
    : map(_map), styling(_styling), altStyling(_altStyling) {}

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
}

void MarkerGroup::updateMarker(int id, Properties&& props)
{
  for(size_t ii = 0; ii < places.size(); ++ii) {
    if(places[ii].id == id) {
      PlaceInfo& res = places[ii];
      res.props = std::move(props);
      res.props.set("priority", ii);
      for(auto& item : commonProps.items())
        res.props.setValue(item.key, item.value);
      //map->markerSetPoint(it->markerId, it->pos);  // to force marker update
      if(res.markerId > 0)
        map->markerSetProperties(res.markerId, Properties(res.props));
      if(res.altMarkerId > 0)
        map->markerSetProperties(res.altMarkerId, Properties(res.props));
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
      break;
    }
  }
}

void MarkerGroup::createMarker(LngLat pos, OnPickedFn cb, Properties&& props, int id)
{
  places.push_back({id, pos, std::move(props), cb, 0, 0});  //, 0, false});
  PlaceInfo& res = places.back();
  for(auto& item : commonProps.items())
    res.props.setValue(item.key, item.value);
  res.props.set("priority", places.size()-1);  //id < 0 ? places.size()-1 : id);
  createMapMarker(res);
}
