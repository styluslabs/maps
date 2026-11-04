// Offline router: builds a routing graph on demand from the transportation layer of z14 vector tiles
// - z14 highway lines are neither simplified nor merged, so ways sharing an OSM node share an exact tile
//  coordinate; the graph is rebuilt by hashing global integer coordinates
// - lines are clipped at tile edges; endpoints on an edge are matched to the neighbouring tile with +/-2 units
// - tiles are loaded lazily as the A* search reaches a tile edge, restricted to a corridor around the endpoints
// - meant for short and medium routes (walking, cycling, local driving), not long distance routing
// - which features are routable, speeds and penalties per mode, oneway and street names for directions are set by
//  application.routing in the scene (see assets/scenes/routing.yaml), using tangram filters
// See ~/maps/HANDOFF-routing.md for background

#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include "pbf/pbf.hpp"
#include "scene/sceneLoader.h"
#include "scene/filters.h"
#include "scene/styleContext.h"
#include "data/tileData.h"
#include "data/propertyItem.h"
#include "tile/tileID.h"

namespace OfflineRouter {

enum Mode { FOOT = 0, BIKE, CAR };

struct RoutePt { double lng, lat, dist, time; };  // dist in meters, time in seconds from start
struct RouteStep { int idx; std::string instr; };
struct RouteResult { std::vector<RoutePt> pts; std::vector<RouteStep> steps; std::string error; };

// returns decompressed MVT tiles for z/x/y from all candidate sources (empty if unavailable)
using TileLoader = std::function<std::vector<std::string>(int x, int y, int z)>;

static constexpr double EARTH_CIRC = 40075016.686;  // meters

// routing configuration from scene
struct Rule { Tangram::Filter filter; double value; };

struct Profile {
  bool valid = false;
  std::vector<Rule> speed;  // first matching rule sets speed in km/h; 0 = not allowed
  std::vector<Rule> factor;  // cost multiplied by all matching factors
  Tangram::Filter oneway, onewayExcept;
  std::string maxspeedKey;  // speed limit attribute, applied as min(scale*maxspeed, max_factor*speed)
  double maxspeedScale = 1, maxspeedMaxFactor = INFINITY;
  double maxSpeed = 0;  // km/h, upper bound of effective speed, for A* heuristic
};

struct Config {
  std::string source;  // tile source name
  std::string layer = "transportation";
  int zoom = 14;
  Tangram::Filter filter;  // routable features
  std::vector<std::string> nameKeys;  // first non-empty is used as name in directions
  std::string labelKey;  // for unnamed ways, "the " + labels[value of labelKey]
  std::unordered_map<std::string, std::string> labels;
  Profile profiles[3];
};

static const char* modeNames[] = { "walk", "bike", "drive" };

struct GPt { double x, y; };  // global coordinates: z14 tile index * extent + tile coordinate

struct Way {
  float spm[3][2];  // seconds per meter for each mode, forward and reverse; < 0 if not allowed
  std::string name;  // for directions
  bool named;  // false if name is just the highway type
};

struct Node {
  GPt p;
  int64_t tile;
  uint8_t edgeFlags;  // which tile edges node lies on
  bool expanded = false;  // neighbouring tiles loaded
  std::vector<int> edges;
};

struct Edge {
  int a, b;
  int way;
  int64_t tile;
  float len;  // meters
  std::vector<GPt> pts;
};

enum EdgeFlag : uint8_t { LEFT = 1, RIGHT = 2, TOP = 4, BOTTOM = 8 };

static int64_t tileKey(int x, int y) { return (int64_t(x) << 32) | uint32_t(y); }

class Router
{
public:
  Router(std::shared_ptr<const Config> _cfg, TileLoader _loader = NULL)
      : cfg(_cfg), zoom(_cfg->zoom), loader(_loader), styleCtx(std::make_unique<Tangram::StyleContext>()) {}
  // waypoints as (longitude, latitude)
  RouteResult route(Mode mode, const std::vector<std::pair<double,double>>& lngLats,
      const std::function<bool()>& canceled = NULL);
  void setLoader(TileLoader _loader) { loader = _loader; }
  size_t numTiles() const { return tiles.size(); }

  GPt lngLatToGlobal(double lng, double lat, double ext) const;
  void globalToLngLat(GPt p, double& lng, double& lat) const;

  std::shared_ptr<const Config> cfg;
  int zoom;
  int extent = 4096;  // all tiles must have the same extent; set from first tile
  bool extentSet = false;
  int maxNewTiles = 1500;  // per route request

private:
  struct Tile { bool loaded = false; bool missing = false; std::vector<int> edges; };
  struct Snap { int edge = -1; size_t seg = 0; GPt p; double dist2 = INFINITY; };

  TileLoader loader;
  std::unique_ptr<Tangram::StyleContext> styleCtx;  // for filter evaluation
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  std::vector<Way> ways;
  std::unordered_map<int64_t, int> nodeMap;  // global coord -> node
  std::unordered_map<int64_t, Tile> tiles;
  int tilesLoaded = 0;
  int bx0, by0, bx1, by1;  // tile corridor for current search

  // A* state
  std::vector<float> gcost;
  std::vector<int> parentEdge;
  std::vector<int> stamp;
  int searchId = 0;

  bool loadTile(int tx, int ty);
  bool decodeTile(const std::string& data, int tx, int ty);
  Way makeWay(const Tangram::Feature& feat);
  int nodeAt(int64_t gx, int64_t gy, int tx, int ty);
  void addEdge(int a, int b, int way, int tx, int ty, std::vector<GPt>&& pts);
  void expandNode(int n);
  bool allowed(int e, Mode mode) const;
  Snap findNearest(GPt p, Mode mode, int tx, int ty) const;
  int snapToGraph(GPt p, Mode mode);
  std::vector<int> astar(int src, int dst, Mode mode, const std::function<bool()>& canceled, std::string& err);
  double metersPerUnit(double gy) const;
  double edgeLength(const std::vector<GPt>& pts) const;
  float edgeCost(int e, bool fwd, Mode mode) const;
  double headingFrom(int e, int u) const;
};

// coordinate conversion

GPt Router::lngLatToGlobal(double lng, double lat, double ext) const
{
  double n = double(1 << zoom) * ext;
  double latrad = lat * M_PI/180.0;
  return { (lng + 180.0)/360.0 * n, (1.0 - std::asinh(std::tan(latrad))/M_PI)/2.0 * n };
}

void Router::globalToLngLat(GPt p, double& lng, double& lat) const
{
  double n = double(1 << zoom) * extent;
  lng = p.x/n * 360.0 - 180.0;
  lat = std::atan(std::sinh(M_PI*(1.0 - 2.0*p.y/n))) * 180.0/M_PI;
}

double Router::metersPerUnit(double gy) const
{
  double n = double(1 << zoom) * extent;
  double lat = std::atan(std::sinh(M_PI*(1.0 - 2.0*gy/n)));
  return EARTH_CIRC*std::cos(lat)/n;
}

double Router::edgeLength(const std::vector<GPt>& pts) const
{
  double len = 0;
  for(size_t ii = 1; ii < pts.size(); ++ii)
    len += std::hypot(pts[ii].x - pts[ii-1].x, pts[ii].y - pts[ii-1].y);
  return pts.empty() ? 0 : len*metersPerUnit(pts[pts.size()/2].y);
}

// profiles

static bool parseConfig(const YAML::Node& node, Config& cfg, std::string& err)
{
  if(!node || !node.IsMap()) { err = "Map style has no routing configuration"; return false; }
  Tangram::SceneFunctions fns;
  auto filter = [&](const YAML::Node& n) {
    return n ? Tangram::SceneLoader::generateFilter(fns, n) : Tangram::Filter::BooleanValue(true);
  };
  auto rules = [&](const YAML::Node& n, std::vector<Rule>& out) {
    for(const auto& r : n)
      out.push_back({filter(r["filter"]), r["value"].as<double>(0)});
  };
  cfg.source = node["source"].as<std::string>("");
  cfg.layer = node["layer"].as<std::string>(cfg.layer);
  cfg.zoom = node["zoom"].as<int>(cfg.zoom);
  cfg.filter = filter(node["filter"]);
  for(const auto& n : node["name"])
    cfg.nameKeys.push_back(n.Scalar());
  cfg.labelKey = node["label_key"].as<std::string>("");
  for(const auto& kv : node["labels"].pairs())
    cfg.labels[kv.first.Scalar()] = kv.second.Scalar();
  bool anyProfile = false;
  for(int mode = FOOT; mode <= CAR; ++mode) {
    const YAML::Node& pn = node["profiles"][modeNames[mode]];
    if(!pn) continue;
    Profile& p = cfg.profiles[mode];
    p.valid = anyProfile = true;
    rules(pn["speed"], p.speed);
    rules(pn["factor"], p.factor);
    if(const YAML::Node& ow = pn["oneway"]) {
      p.oneway = filter(ow["filter"]);
      if(ow["except"]) p.onewayExcept = filter(ow["except"]);
    }
    if(const YAML::Node& ms = pn["maxspeed"]) {
      p.maxspeedKey = ms["key"].as<std::string>("maxspeed");
      p.maxspeedScale = ms["scale"].as<double>(1);
      p.maxspeedMaxFactor = ms["max_factor"].as<double>(INFINITY);
    }
    for(const Rule& r : p.speed)
      p.maxSpeed = std::max(p.maxSpeed, r.value);
    if(!p.maxspeedKey.empty())
      p.maxSpeed = std::max(p.maxSpeed, std::min(150*p.maxspeedScale, p.maxSpeed*p.maxspeedMaxFactor));
    double minFactor = 1;
    for(const Rule& r : p.factor)
      minFactor = std::min(minFactor, r.value);
    if(minFactor > 0) p.maxSpeed /= minFactor;  // keep heuristic admissible if factors < 1 are used
  }
  if(!fns.empty()) { err = "Routing filters do not support JS functions"; return false; }
  if(cfg.source.empty()) { err = "Routing configuration has no tile source"; return false; }
  if(!anyProfile) { err = "Routing configuration has no profiles"; return false; }
  return true;
}

Way Router::makeWay(const Tangram::Feature& feat)
{
  Way way;
  std::string str;
  for(int mode = FOOT; mode <= CAR; ++mode) {
    const Profile& p = cfg->profiles[mode];
    double v = 0;
    if(p.valid) {
      for(const Rule& r : p.speed) {
        if(r.filter.eval(feat, *styleCtx)) { v = r.value; break; }
      }
      if(v > 0 && !p.maxspeedKey.empty() && feat.props.getAsString(p.maxspeedKey, str)) {
        double ms = std::atof(str.c_str());
        if(str.find("mph") != std::string::npos) ms *= 1.609;
        if(ms >= 5 && ms <= 150) v = std::min(p.maxspeedScale*ms, p.maxspeedMaxFactor*v);
      }
      for(size_t ii = 0; v > 0 && ii < p.factor.size(); ++ii) {
        if(p.factor[ii].value > 0 && p.factor[ii].filter.eval(feat, *styleCtx)) v /= p.factor[ii].value;
      }
    }
    float spm = v > 0 ? float(3.6/v) : -1;
    way.spm[mode][0] = spm;
    bool oneway = spm > 0 && p.oneway.isValid() && p.oneway.eval(feat, *styleCtx)
        && !(p.onewayExcept.isValid() && p.onewayExcept.eval(feat, *styleCtx));
    way.spm[mode][1] = oneway ? -1 : spm;
  }
  for(const std::string& key : cfg->nameKeys) {
    if(feat.props.getAsString(key, way.name) && !way.name.empty()) break;
  }
  way.named = !way.name.empty();
  if(!way.named) {
    std::string label = "road";
    if(!cfg->labelKey.empty() && feat.props.getAsString(cfg->labelKey, str) && !str.empty()) {
      auto it = cfg->labels.find(str);
      if(it != cfg->labels.end()) label = it->second;
      else {
        std::replace(str.begin(), str.end(), '_', ' ');
        label = str;
      }
    }
    way.name = "the " + label;
  }
  return way;
}

// tile decoding

static Tangram::Value readValue(protobuf::message msg)
{
  Tangram::Value val;
  while(msg.next()) {
    switch(msg.tag) {
      case 1: val = Tangram::Value(msg.string()); break;
      case 2: val = Tangram::Value(double(msg.float32())); break;
      case 3: val = Tangram::Value(msg.float64()); break;
      case 4: val = Tangram::Value(double(msg.int64())); break;
      case 5: val = Tangram::Value(double(msg.varint())); break;
      case 6: val = Tangram::Value(double(msg.svarint())); break;
      case 7: val = Tangram::Value(msg.boolean() ? 1.0 : 0.0); break;
      default: msg.skip(); break;
    }
  }
  return val;
}

bool Router::decodeTile(const std::string& data, int tx, int ty)
{
  protobuf::message tileMsg(data.data(), data.size());
  while(tileMsg.next()) {
    if(tileMsg.tag != 3) { tileMsg.skip(); continue; }
    protobuf::message layerMsg = tileMsg.getMessage();
    protobuf::message probe = layerMsg;
    std::string name;
    while(probe.next()) {
      if(probe.tag == 1) { name = probe.string(); break; }
      probe.skip();
    }
    if(name != cfg->layer) continue;

    std::vector<std::string> keys;
    std::vector<Tangram::Value> values;
    std::vector<protobuf::message> features;
    int lyrExtent = 4096;
    while(layerMsg.next()) {
      switch(layerMsg.tag) {
        case 2: features.push_back(layerMsg.getMessage()); break;
        case 3: keys.push_back(layerMsg.string()); break;
        case 4: values.push_back(readValue(layerMsg.getMessage())); break;
        case 5: lyrExtent = int(layerMsg.varint()); break;
        default: layerMsg.skip(); break;
      }
    }
    if(!extentSet) { extent = lyrExtent; extentSet = true; }
    if(lyrExtent != extent) return false;

    std::vector<std::vector<GPt>> lines;
    std::vector<Way> lineWays;
    std::unordered_map<int64_t, int> vertCount;
    styleCtx->setTileID(Tangram::TileID(tx, ty, zoom));
    for(protobuf::message featMsg : features) {
      Tangram::Feature feat;
      feat.geometryType = Tangram::GeometryType::lines;
      int geomType = 0;
      protobuf::message geomMsg;
      while(featMsg.next()) {
        if(featMsg.tag == 2) {
          protobuf::message tagsMsg = featMsg.getMessage();
          while(tagsMsg) {
            size_t ki = tagsMsg.varint();
            if(!tagsMsg) break;
            size_t vi = tagsMsg.varint();
            if(ki >= keys.size() || vi >= values.size()) continue;
            feat.props.setValue(keys[ki], values[vi]);
          }
        }
        else if(featMsg.tag == 3) geomType = int(featMsg.varint());
        else if(featMsg.tag == 4) geomMsg = featMsg.getMessage();
        else featMsg.skip();
      }
      if(geomType != 2) continue;
      styleCtx->setFeature(feat);
      if(!cfg->filter.eval(feat, *styleCtx)) continue;
      Way way = makeWay(feat);
      bool usable = false;
      for(int m = FOOT; m <= CAR; ++m) usable = usable || way.spm[m][0] > 0 || way.spm[m][1] > 0;
      if(!usable) continue;

      // decode geometry; MultiLineString gives multiple lines
      int64_t cx = 0, cy = 0;
      std::vector<GPt>* line = NULL;
      while(geomMsg) {
        uint32_t cmdlen = uint32_t(geomMsg.varint());
        uint32_t cmd = cmdlen & 0x7, count = cmdlen >> 3;
        if(cmd == 7) continue;  // closePath not expected for lines
        for(uint32_t ii = 0; ii < count && geomMsg; ++ii) {
          cx += geomMsg.svarint();
          cy += geomMsg.svarint();
          if(cmd == 1) {
            lines.emplace_back();
            lineWays.push_back(way);
            line = &lines.back();
          }
          if(!line) continue;
          GPt p = { double(int64_t(tx)*extent + cx), double(int64_t(ty)*extent + cy) };
          if(!line->empty() && line->back().x == p.x && line->back().y == p.y) continue;
          line->push_back(p);
          vertCount[(int64_t(p.x) << 32) | uint32_t(int64_t(p.y))] += 1;
        }
      }
    }

    // split lines into edges at shared vertices, endpoints, and tile edge vertices
    for(size_t li = 0; li < lines.size(); ++li) {
      auto& pts = lines[li];
      if(pts.size() < 2) continue;
      ways.push_back(lineWays[li]);
      int wayIdx = int(ways.size()) - 1;
      int64_t x0 = int64_t(tx)*extent, y0 = int64_t(ty)*extent;
      size_t start = 0;
      int startNode = nodeAt(int64_t(pts[0].x), int64_t(pts[0].y), tx, ty);
      for(size_t ii = 1; ii < pts.size(); ++ii) {
        int64_t gx = int64_t(pts[ii].x), gy = int64_t(pts[ii].y);
        bool onEdge = gx == x0 || gx == x0 + extent || gy == y0 || gy == y0 + extent;
        if(ii + 1 < pts.size() && !onEdge && vertCount[(gx << 32) | uint32_t(gy)] < 2) continue;
        int endNode = nodeAt(gx, gy, tx, ty);
        addEdge(startNode, endNode, wayIdx, tx, ty, std::vector<GPt>(pts.begin() + start, pts.begin() + ii + 1));
        start = ii;
        startNode = endNode;
      }
    }
    return true;
  }
  return false;
}

int Router::nodeAt(int64_t gx, int64_t gy, int tx, int ty)
{
  int64_t key = (gx << 32) | uint32_t(gy);
  auto it = nodeMap.find(key);
  if(it != nodeMap.end()) return it->second;

  int64_t x0 = int64_t(tx)*extent, y0 = int64_t(ty)*extent;
  uint8_t flags = (gx == x0 ? LEFT : 0) | (gx == x0 + extent ? RIGHT : 0)
      | (gy == y0 ? TOP : 0) | (gy == y0 + extent ? BOTTOM : 0);
  int64_t tkey = tileKey(tx, ty);
  // lines are clipped exactly at tile edges, but endpoints in neighbouring tiles can differ by 1-2 units
  if(flags) {
    for(int d : {-1, 1, -2, 2}) {
      int64_t nx = gx + ((flags & (TOP|BOTTOM)) ? d : 0), ny = gy + ((flags & (LEFT|RIGHT)) ? d : 0);
      auto it2 = nodeMap.find((nx << 32) | uint32_t(ny));
      if(it2 != nodeMap.end() && nodes[it2->second].tile != tkey) {
        nodeMap[key] = it2->second;
        return it2->second;
      }
    }
  }
  nodes.push_back(Node{{double(gx), double(gy)}, tkey, flags, false, {}});
  nodeMap[key] = int(nodes.size()) - 1;
  return int(nodes.size()) - 1;
}

void Router::addEdge(int a, int b, int way, int tx, int ty, std::vector<GPt>&& pts)
{
  if(a == b && pts.size() < 3) return;
  int e = int(edges.size());
  edges.push_back(Edge{a, b, way, tileKey(tx, ty), float(edgeLength(pts)), std::move(pts)});
  nodes[a].edges.push_back(e);
  if(b != a) nodes[b].edges.push_back(e);
  tiles[tileKey(tx, ty)].edges.push_back(e);
}

bool Router::loadTile(int tx, int ty)
{
  auto it = tiles.find(tileKey(tx, ty));
  if(it != tiles.end() && it->second.loaded) return true;
  if(tx < bx0 || tx > bx1 || ty < by0 || ty > by1) return false;
  if(tilesLoaded >= maxNewTiles) return false;
  tiles[tileKey(tx, ty)].loaded = true;
  ++tilesLoaded;
  auto blobs = loader(tx, ty, zoom);
  for(const std::string& data : blobs) {
    if(decodeTile(data, tx, ty)) return true;
  }
  // no data (e.g. not downloaded or no network): retry on next route request
  if(blobs.empty()) tiles[tileKey(tx, ty)].missing = true;
  return false;
}

void Router::expandNode(int n)
{
  if(nodes[n].expanded) return;
  nodes[n].expanded = true;
  uint8_t f = nodes[n].edgeFlags;
  if(!f) return;
  int tx = int(nodes[n].tile >> 32), ty = int(uint32_t(nodes[n].tile));
  int dx = (f & LEFT) ? -1 : (f & RIGHT) ? 1 : 0;
  int dy = (f & TOP) ? -1 : (f & BOTTOM) ? 1 : 0;
  if(dx) loadTile(tx + dx, ty);
  if(dy) loadTile(tx, ty + dy);
  if(dx && dy) loadTile(tx + dx, ty + dy);
}

// routing

bool Router::allowed(int e, Mode mode) const
{
  const Way& w = ways[edges[e].way];
  return w.spm[mode][0] > 0 || w.spm[mode][1] > 0;
}

float Router::edgeCost(int e, bool fwd, Mode mode) const
{
  float spm = ways[edges[e].way].spm[mode][fwd ? 0 : 1];
  return spm > 0 ? edges[e].len*spm : -1;
}

static double distToSegment2(GPt p, GPt a, GPt b, GPt& proj)
{
  double dx = b.x - a.x, dy = b.y - a.y;
  double l2 = dx*dx + dy*dy;
  double t = l2 > 0 ? std::max(0.0, std::min(1.0, ((p.x - a.x)*dx + (p.y - a.y)*dy)/l2)) : 0;
  proj = { a.x + t*dx, a.y + t*dy };
  return (p.x - proj.x)*(p.x - proj.x) + (p.y - proj.y)*(p.y - proj.y);
}

Router::Snap Router::findNearest(GPt p, Mode mode, int tx, int ty) const
{
  Snap best;
  auto it = tiles.find(tileKey(tx, ty));
  if(it == tiles.end()) return best;
  for(int e : it->second.edges) {
    if(!allowed(e, mode)) continue;
    const auto& pts = edges[e].pts;
    for(size_t ii = 0; ii + 1 < pts.size(); ++ii) {
      GPt proj;
      double d2 = distToSegment2(p, pts[ii], pts[ii+1], proj);
      if(d2 < best.dist2) { best = Snap{e, ii, proj, d2}; }
    }
  }
  return best;
}

// find nearest edge and split it, returning new node (or existing node if snapped point is an endpoint)
int Router::snapToGraph(GPt p, Mode mode)
{
  int tx = int(std::floor(p.x/extent)), ty = int(std::floor(p.y/extent));
  if(!loadTile(tx, ty)) return -1;
  Snap best = findNearest(p, mode, tx, ty);
  // check neighbouring tiles if they could contain a closer edge
  double fx = p.x - double(tx)*extent, fy = p.y - double(ty)*extent;
  for(int dy = -1; dy <= 1; ++dy) {
    for(int dx = -1; dx <= 1; ++dx) {
      if(!dx && !dy) continue;
      double ex = dx < 0 ? fx : dx > 0 ? extent - fx : 0;
      double ey = dy < 0 ? fy : dy > 0 ? extent - fy : 0;
      if(ex*ex + ey*ey >= best.dist2) continue;
      if(!loadTile(tx + dx, ty + dy)) continue;
      Snap s = findNearest(p, mode, tx + dx, ty + dy);
      if(s.dist2 < best.dist2) best = s;
    }
  }
  // limit snapping distance to ~1 km
  if(best.edge < 0 || std::sqrt(best.dist2)*metersPerUnit(p.y) > 1000) return -1;

  Edge& edge = edges[best.edge];
  const double eps2 = 1E-4;
  auto near = [&](GPt a, GPt b){ return (a.x - b.x)*(a.x - b.x) + (a.y - b.y)*(a.y - b.y) < eps2; };
  if(near(best.p, edge.pts.front())) return edge.a;
  if(near(best.p, edge.pts.back())) return edge.b;

  int e = best.edge;
  int n = int(nodes.size());
  nodes.push_back(Node{best.p, edge.tile, 0, true, {}});
  std::vector<GPt> pts2 = {best.p};
  pts2.insert(pts2.end(), edges[e].pts.begin() + best.seg + 1, edges[e].pts.end());
  edges[e].pts.resize(best.seg + 1);
  edges[e].pts.push_back(best.p);
  int b = edges[e].b;
  edges[e].b = n;
  edges[e].len = float(edgeLength(edges[e].pts));
  int e2 = int(edges.size());
  edges.push_back(Edge{n, b, edges[e].way, edges[e].tile, float(edgeLength(pts2)), std::move(pts2)});
  auto& bedges = nodes[b].edges;
  // for a loop edge (a == b), node lists e once; it now needs both e and e2
  if(edges[e].a == b) bedges.push_back(e2);
  else std::replace(bedges.begin(), bedges.end(), e, e2);
  nodes[n].edges = {e, e2};
  tiles[edges[e].tile].edges.push_back(e2);
  return n;
}

std::vector<int> Router::astar(int src, int dst, Mode mode, const std::function<bool()>& canceled, std::string& err)
{
  const float maxSpeed = float(cfg->profiles[mode].maxSpeed/3.6);  // m/s, for heuristic
  ++searchId;
  GPt target = nodes[dst].p;
  double mpu = std::min(metersPerUnit(target.y), metersPerUnit(nodes[src].p.y));
  auto heuristic = [&](int n) {
    return float(std::hypot(nodes[n].p.x - target.x, nodes[n].p.y - target.y)*mpu/maxSpeed);
  };
  auto ensureSize = [&](){
    if(stamp.size() < nodes.size()) {
      size_t sz = nodes.size() + nodes.size()/2;
      stamp.resize(sz, 0);
      gcost.resize(sz);
      parentEdge.resize(sz);
    }
  };
  ensureSize();

  using QItem = std::pair<float, int>;
  std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> queue;
  stamp[src] = searchId;
  gcost[src] = 0;
  parentEdge[src] = -1;
  queue.push({heuristic(src), src});
  size_t iter = 0;
  while(!queue.empty()) {
    float f = queue.top().first;
    int u = queue.top().second;
    queue.pop();
    if(u == dst) break;
    float gu = gcost[u];
    if(f - heuristic(u) > gu + 1E-3f) continue;  // stale entry
    if((++iter & 0xFFF) == 0 && canceled && canceled()) { err = "canceled"; return {}; }
    expandNode(u);
    ensureSize();
    for(size_t ii = 0; ii < nodes[u].edges.size(); ++ii) {
      int e = nodes[u].edges[ii];
      const Edge& edge = edges[e];
      bool fwd = edge.a == u;
      int v = fwd ? edge.b : edge.a;
      if(v == u) continue;
      float c = edgeCost(e, fwd, mode);
      if(c < 0) continue;
      float gv = gu + c;
      if(stamp[v] != searchId || gv < gcost[v]) {
        stamp[v] = searchId;
        gcost[v] = gv;
        parentEdge[v] = e;
        queue.push({gv + heuristic(v), v});
      }
    }
  }
  if(stamp[dst] != searchId) {
    err = tilesLoaded >= maxNewTiles ? "Route too long for offline routing" : "No route found";
    return {};
  }
  std::vector<int> path;
  for(int n = dst; parentEdge[n] >= 0; ) {
    int e = parentEdge[n];
    path.push_back(e);
    n = edges[e].a == n ? edges[e].b : edges[e].a;
  }
  std::reverse(path.begin(), path.end());
  return path;
}

// directions

static double heading(GPt a, GPt b) { return std::atan2(b.x - a.x, a.y - b.y)*180/M_PI; }  // 0 = north, cw

// heading of edge e leaving node u, measured to a point ~10 m along the edge
double Router::headingFrom(int e, int u) const
{
  const auto& pts = edges[e].pts;
  bool fwd = edges[e].a == u;
  size_t n = pts.size();
  auto pt = [&](size_t ii){ return fwd ? pts[ii] : pts[n - 1 - ii]; };
  double mpu = metersPerUnit(pts[0].y);
  size_t hi = 1;
  while(hi + 1 < n && std::hypot(pt(hi).x - pt(0).x, pt(hi).y - pt(0).y)*mpu < 10)
    ++hi;
  return heading(pt(0), pt(hi));
}

static double turnAngle(double h1, double h0) { return std::fmod(h1 - h0 + 540.0, 360.0) - 180.0; }

static std::string turnInstr(double turn, const std::string& name, bool keep = false)
{
  double a = std::abs(turn);
  const char* side = turn > 0 ? "right" : "left";
  std::string s;
  if(keep && a < 30) s = std::string("Keep ") + side;
  else if(a < 30) s = "Continue";
  else if(a < 60) s = std::string("Turn slight ") + side;
  else if(a < 120) s = std::string("Turn ") + side;
  else if(a < 165) s = std::string("Turn sharp ") + side;
  else s = "Make a U-turn";
  return s + " onto " + name;
}

RouteResult Router::route(Mode mode, const std::vector<std::pair<double,double>>& lngLats,
    const std::function<bool()>& canceled)
{
  RouteResult res;
  if(lngLats.size() < 2) { res.error = "Not enough waypoints"; return res; }
  if(!cfg->profiles[mode].valid) {
    res.error = std::string("Routing configuration has no ") + modeNames[mode] + " profile";
    return res;
  }
  tilesLoaded = 0;
  for(auto it = tiles.begin(); it != tiles.end();)
    it = it->second.missing ? tiles.erase(it) : ++it;
  // limit search to tiles around waypoints; first use coordinates in units of tiles (extent = 1)
  std::vector<GPt> waypts;
  double minx = INFINITY, miny = INFINITY, maxx = -INFINITY, maxy = -INFINITY;
  for(auto& ll : lngLats) {
    GPt p = lngLatToGlobal(ll.first, ll.second, 1);
    waypts.push_back(p);
    minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
    miny = std::min(miny, p.y); maxy = std::max(maxy, p.y);
  }
  double margin = std::max(2.0, 0.5*std::max(maxx - minx, maxy - miny));
  bx0 = int(std::floor(minx - margin)); bx1 = int(std::floor(maxx + margin));
  by0 = int(std::floor(miny - margin)); by1 = int(std::floor(maxy + margin));
  if(!extentSet)
    loadTile(int(waypts[0].x), int(waypts[0].y));  // sets extent
  for(GPt& p : waypts)
    p = GPt{p.x*extent, p.y*extent};

  std::vector<int> snapped;
  for(size_t ii = 0; ii < waypts.size(); ++ii) {
    int n = snapToGraph(waypts[ii], mode);
    if(n < 0) {
      res.error = "No road or path found near waypoint " + std::to_string(ii + 1);
      return res;
    }
    snapped.push_back(n);
  }

  double dist = 0, time = 0;
  auto addPt = [&](GPt p, bool first) {
    RoutePt rp;
    globalToLngLat(p, rp.lng, rp.lat);
    if(!first && !res.pts.empty()) {
      const RoutePt& prev = res.pts.back();
      if(prev.lng == rp.lng && prev.lat == rp.lat) return;
    }
    rp.dist = dist;
    rp.time = time;
    res.pts.push_back(rp);
  };

  for(size_t leg = 0; leg + 1 < snapped.size(); ++leg) {
    if(snapped[leg] == snapped[leg+1]) continue;
    std::vector<int> path = astar(snapped[leg], snapped[leg+1], mode, canceled, res.error);
    if(path.empty()) {
      if(res.error.empty()) res.error = "No route found";
      res.pts.clear();
      return res;
    }
    int u = snapped[leg];
    double prevHeading = 0, stepHeading = 0;  // stepHeading: incoming heading at last turn step
    std::string prevName, stepName;
    bool lastIsTurn = false;
    for(size_t pi = 0; pi < path.size(); ++pi) {
      const Edge& edge = edges[path[pi]];
      bool fwd = edge.a == u;
      std::vector<GPt> pts = edge.pts;
      if(!fwd) std::reverse(pts.begin(), pts.end());
      const std::string& name = ways[edge.way].name;
      double inHeading = headingFrom(path[pi], u);
      bool stepped = false;
      if(pi == 0) {
        if(leg == 0) {
          addPt(pts[0], true);
          res.steps.push_back({0, "Start on " + name});
        }
      }
      else {
        double turn = turnAngle(inHeading, prevHeading);
        // other options at this node
        int nother = 0, straighter = 0;
        for(int e2 : nodes[u].edges) {
          if(e2 == path[pi] || e2 == path[pi-1]) continue;
          bool fwd2 = edges[e2].a == u;
          if((fwd2 ? edges[e2].b : edges[e2].a) == u || edgeCost(e2, fwd2, mode) < 0) continue;
          ++nother;
          double t2 = turnAngle(headingFrom(e2, u), prevHeading);
          if(std::abs(t2) < std::abs(turn)) straighter = t2 < turn ? 1 : -1;
        }
        // ignore name change for small turns onto unnamed ways (e.g. missing ref on part of a road)
        bool sameName = name == prevName || (std::abs(turn) < 30 && !ways[edge.way].named);
        // no instruction if following the straightest option along the same road
        if(nother > 0 && (!sameName || straighter || std::abs(turn) >= 60)) {
          int idx = int(res.pts.size()) - 1;
          stepped = true;
          // merge with previous turn if very close (e.g., roundabouts, dual carriageways, short connectors)
          if(lastIsTurn && dist - res.pts[res.steps.back().idx].dist < 30) {
            double mturn = turnAngle(inHeading, stepHeading);
            if(std::abs(mturn) < 30 && name == stepName) {
              res.steps.pop_back();  // no net change
              lastIsTurn = false;
            }
            else
              res.steps.back().instr = turnInstr(mturn, name);
          }
          else {
            res.steps.push_back({idx, turnInstr(turn, name, straighter != 0)});
            stepHeading = prevHeading;
            stepName = prevName;
            lastIsTurn = true;
          }
        }
      }
      double spm = ways[edge.way].spm[mode][fwd ? 0 : 1];
      double mpu = metersPerUnit(pts[pts.size()/2].y);
      for(size_t ii = 1; ii < pts.size(); ++ii) {
        double d = std::hypot(pts[ii].x - pts[ii-1].x, pts[ii].y - pts[ii-1].y)*mpu;
        dist += d;
        time += d*spm;
        addPt(pts[ii], false);
      }
      // heading near end of edge
      size_t lo = pts.size() - 2;
      while(lo > 0 && std::hypot(pts.back().x - pts[lo].x, pts.back().y - pts[lo].y)*mpu < 10)
        --lo;
      prevHeading = heading(pts[lo], pts.back());
      if(ways[edge.way].named || stepped || pi == 0)
        prevName = name;
      u = fwd ? edge.b : edge.a;
    }
    res.steps.push_back({int(res.pts.size()) - 1,
        leg + 2 < snapped.size() ? "Arrive at waypoint " + std::to_string(leg + 2) : std::string("Arrive at destination")});
  }
  if(res.pts.empty()) res.error = "Waypoints are too close together";
  return res;
}

}  // namespace OfflineRouter

#ifndef OFFLINE_ROUTER_STANDALONE
// app integration: called from MapsTracks::createRoute() when the "Offline" route provider is selected

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "mapsapp.h"
#include "tracks.h"
#include "util.h"
#include "sqlite3/sqlitepp.h"
#include "util/zlibHelper.h"
#include "data/networkDataSource.h"
#include "offlinerouter.h"
#include "scene/scene.h"

using namespace Tangram;

struct RouterTileSource {
  std::string cacheFile;
  std::string url;
  UrlOptions urlOptions;
};

static std::mutex routerMutex;
static std::atomic<int> routeSerial{0};
static OfflineRouter::Router* routerInst = NULL;  // not freed, so a running worker never sees a destroyed graph
static std::string routerSrcKey;

static std::vector<char> fetchTileSync(const std::string& url, const UrlOptions& opts)
{
  std::mutex mtx;
  std::condition_variable cv;
  bool done = false;
  auto result = std::make_shared<std::vector<char>>();
  UrlRequestHandle hnd = MapsApp::platform->startUrlRequest(Url(url), opts.httpOptions,
      [&mtx, &cv, &done, result](UrlResponse&& response) {
    std::lock_guard<std::mutex> lock(mtx);
    if(!response.error) *result = std::move(response.content);
    done = true;
    cv.notify_all();
  });
  std::unique_lock<std::mutex> lock(mtx);
  if(!cv.wait_for(lock, std::chrono::seconds(20), [&]{ return done; })) {
    lock.unlock();
    MapsApp::platform->cancelUrlRequest(hnd);  // callback still runs, so wait for it
    lock.lock();
    cv.wait(lock, [&]{ return done; });
  }
  return std::move(*result);
}

static std::vector<std::string> loadRouterTile(std::vector<RouterTileSource>& srcs,
    std::vector<std::unique_ptr<SQLiteDB>>& dbs, int x, int y, int z)
{
  std::vector<std::string> res;
  auto addData = [&](const char* blob, size_t len){
    std::vector<char> out;
    if(zlib_inflate(blob, len, out) == 0) res.emplace_back(out.data(), out.size());
    else res.emplace_back(blob, len);
  };
  for(size_t ii = 0; ii < srcs.size(); ++ii) {
    if(!dbs[ii]) continue;
    dbs[ii]->stmt("SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;")
        .bind(z, x, (1 << z) - 1 - y).exec([&](sqlite3_stmt* stmt){
      addData((const char*)sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));
    });
  }
  if(!res.empty()) return res;
  for(auto& src : srcs) {
    if(src.url.empty()) continue;
    std::string url = NetworkDataSource::buildUrlForTile(TileID(x, y, z), src.url, src.urlOptions, 0);
    std::vector<char> data = fetchTileSync(url, src.urlOptions);
    if(!data.empty()) {
      addData(data.data(), data.size());
      break;
    }
  }
  return res;
}

void cancelOfflineRoute() { ++routeSerial; }

void offlineRoute(const std::string& routeMode, const std::vector<LngLat>& lngLats)
{
  using namespace OfflineRouter;
  std::string modestr = routeMode.substr(0, routeMode.find('-'));
  Mode mode = modestr == "walk" ? FOOT : modestr == "bike" ? BIKE : CAR;
  std::vector<std::pair<double,double>> waypts;
  for(const LngLat& ll : lngLats)
    waypts.emplace_back(ll.longitude, ll.latitude);

  int serial = ++routeSerial;
  auto onError = [serial](std::string msg){
    LOGW("Offline router: %s", msg.c_str());
    MapsApp::runOnMainThread([=](){
      if(serial == routeSerial) MapsApp::inst->mapsTracks->routePluginError(msg.c_str());
    });
  };

  // routing config from scene; graph is rebuilt if config or tile source changes
  const YAML::Node& cfgNode = MapsApp::inst->sceneConfig()["application"]["routing"];
  auto cfg = std::make_shared<Config>();
  std::string err;
  if(!parseConfig(cfgNode, *cfg, err)) { onError(err); return; }
  std::vector<RouterTileSource> srcs;
  std::string srckey = yamlToStr(cfgNode);
  for(auto& tilesrc : MapsApp::inst->map->getScene()->tileSources()) {
    if(tilesrc->name() != cfg->source) continue;
    auto& info = tilesrc->offlineInfo();
    if(info.format != TileSource::Format::Mvt) continue;
    srcs.push_back({info.cacheFile, info.url, info.urlOptions});
    srckey += "\n" + info.cacheFile + "|" + info.url;
  }
  if(srcs.empty()) { onError("Tile source for routing not found: " + cfg->source); return; }

  std::thread([=]() mutable {
    std::lock_guard<std::mutex> lock(routerMutex);
    if(serial != routeSerial) return;
    std::vector<std::unique_ptr<SQLiteDB>> dbs;
    for(auto& src : srcs) {
      dbs.emplace_back();
      if(src.cacheFile.empty()) continue;
      auto db = std::make_unique<SQLiteDB>();
      if(db->open(src.cacheFile, SQLITE_OPEN_READONLY) != SQLITE_OK) continue;
      sqlite3_busy_timeout(db->db, 1000);
      dbs.back() = std::move(db);
    }
    // keep graph between requests (e.g. while editing waypoints), but not if source changed or too large
    if(routerInst && (routerSrcKey != srckey || routerInst->numTiles() > 600)) {
      delete routerInst;
      routerInst = NULL;
    }
    if(!routerInst) routerInst = new Router(cfg);
    routerSrcKey = srckey;
    routerInst->setLoader([&](int x, int y, int z){ return loadRouterTile(srcs, dbs, x, y, z); });
    Timestamp t0 = mSecSinceEpoch(); (void)t0;
    RouteResult res = routerInst->route(mode, waypts, [serial](){ return serial != routeSerial; });
    routerInst->setLoader(NULL);
    LOGD("Offline route: %d pts, %d tiles in graph, %d ms", int(res.pts.size()),
        int(routerInst->numTiles()), int(mSecSinceEpoch() - t0));
    if(serial != routeSerial) return;
    if(!res.error.empty()) { onError(res.error); return; }
    MapsApp::runOnMainThread([serial, res=std::move(res)](){
      if(serial != routeSerial) return;
      std::vector<Waypoint> route;
      for(const RoutePt& p : res.pts) {
        route.emplace_back(Location{p.time, p.lat, p.lng, 0, 0, 0, 0, 0, 0, 0}, p.dist);
      }
      auto* tracks = MapsApp::inst->mapsTracks.get();
      tracks->addRoute(std::move(route));
      for(const RouteStep& step : res.steps)
        tracks->addRouteStep(step.instr.c_str(), step.idx);
    });
  }).detach();
}

#endif
