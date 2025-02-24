#include "tilebuilder.h"
#include <geom/polygon/RingCoordinateIterator.h>
#include <geom/polygon/RingBuilder.h>
#include <geom/polygon/Segment.h>

#define MINIZ_GZ_IMPLEMENTATION
#include "ulib/miniz_gzip.h"

// TileBuilder

using namespace geodesk;

TileBuilder::TileBuilder(TileID _id, const std::vector<std::string>& layers) : m_id(_id)
{
  for(auto& l : layers)
    m_layers.emplace(l, vtzero::layer_builder{m_tile, l, 2, tileExtent});  // MVT v2

  double units = Mercator::MAP_WIDTH/MapProjection::EARTH_CIRCUMFERENCE_METERS;
  m_origin = units*MapProjection::tileSouthWestCorner(m_id);
  m_scale = 1/(units*MapProjection::metersPerTileAtZoom(m_id.z));

  simplifyThresh = _id.z < 14 ? 1/512.0f : 0;  // no simplification for highest zoom (which can be overzoomed)
}

static LngLat tileCoordToLngLat(const TileID& tileId, const glm::vec2& tileCoord)
{
  using namespace Tangram;
  double scale = MapProjection::metersPerTileAtZoom(tileId.z);
  ProjectedMeters tileOrigin = MapProjection::tileSouthWestCorner(tileId);
  ProjectedMeters meters = glm::dvec2(tileCoord) * scale + tileOrigin;
  return MapProjection::projectedMetersToLngLat(meters);
}

static Box tileBox(const TileID& id, double eps)
{
  LngLat minBBox = tileCoordToLngLat(id, {eps, eps});
  LngLat maxBBox = tileCoordToLngLat(id, {1-eps, 1-eps});
  return Box::ofWSEN(minBBox.longitude, minBBox.latitude, maxBBox.longitude, maxBBox.latitude);
}

std::string TileBuilder::build(const Features& world, bool compress)
{
  auto time0 = std::chrono::steady_clock::now();

  double eps = 0.01/MapProjection::metersPerTileAtZoom(m_id.z);
  Box bbox = tileBox(m_id, eps);
  Features tileFeats = world(bbox);

  int nfeats = 0;
  for(Feature f : tileFeats) {  //for(auto it = tileFeats.begin(); it != tileFeats.end(); ++it) {
    //m_feat = *it;  //new(&m_feat) Feature(*it); ... m_feat.~Feature();
    m_feat = &f;
    processFeature();
    if(f.isWay() && f["natural"] == "coastline") { addCoastline(f); }
    m_area = NAN;
    ++nfeats;
  }
  m_feat = nullptr;
  if(m_build && m_hasGeom) {
    ++m_totalFeats;
    m_build->commit();
  }
  m_build.reset();
  // ocean polygons
  buildCoastline();

  std::string mvt = m_tile.serialize();  // very fast, not worth separate timing
  if(mvt.size() == 0) {
    LOG("No features for tile %s", m_id.toString().c_str());
    return "";
  }
  auto time1 = std::chrono::steady_clock::now();
  int origsize = mvt.size();
  if(compress) {
    std::stringstream in_strm(mvt);
    std::stringstream out_strm;
    gzip(in_strm, out_strm);
    mvt = std::move(out_strm).str();  // C++20
  }
  auto time2 = std::chrono::steady_clock::now();

  double dt01 = std::chrono::duration<double>(time1 - time0).count()*1000;
  double dt12 = std::chrono::duration<double>(time2 - time1).count()*1000;
  double dt02 = std::chrono::duration<double>(time2 - time0).count()*1000;
  LOG("Tile %s (%d bytes) built in %.1f ms (%.1f ms process %d/%d features w/ %d points, %.1f ms gzip %d bytes)",
      m_id.toString().c_str(), int(mvt.size()), dt02, dt01, m_totalFeats, nfeats, m_totalPts, dt12, origsize);

  return mvt;
}

// simplification

static real dist2(vt_point p) { return p.x*p.x + p.y*p.y; }

// distance from point `pt` to line segment `start`-`end` to `pt`
static real distToSegment2(vt_point start, vt_point end, vt_point pt)
{
  const real l2 = dist2(end - start);
  if(l2 == 0.0) // zero length segment
    return dist2(start - pt);
  // Consider the line extending the segment, parameterized as start + t*(end - start).
  // We find projection of pt onto this line and clamp t to [0,1] to limit to segment
  const real t = std::max(real(0), std::min(real(1), dot(pt - start, end - start)/l2));
  const vt_point proj = start + t * (end - start);  // Projection falls on the segment
  return dist2(proj - pt);
}

static void simplifyRDP(std::vector<vt_point>& pts, std::vector<int>& keep, int start, int end, real thresh)
{
  real maxdist2 = 0;
  int argmax = 0;
  auto& p0 = pts[start];
  auto& p1 = pts[end];
  for(int ii = start + 1; ii < end; ++ii) {
    real d2 = distToSegment2(p0, p1, pts[ii]);
    if(d2 > maxdist2) {
      maxdist2 = d2;
      argmax = ii;
    }
  }
  if(maxdist2 < thresh*thresh) { return; }
  keep[argmax] = 1;
  simplifyRDP(pts, keep, start, argmax, thresh);
  simplifyRDP(pts, keep, argmax, end, thresh);
}

static void simplify(std::vector<vt_point>& pts, real thresh)
{
  if(thresh <= 0 || pts.size() < 3) { return; }
  std::vector<int> keep(pts.size(), 0);
  keep.front() = 1;  keep.back() = 1;
  simplifyRDP(pts, keep, 0, pts.size() - 1, thresh);
  size_t dst = 0;
  for(size_t src = 0; src < pts.size(); ++src) {
    if(keep[src]) { pts[dst++] = pts[src]; }
  }
  pts.resize(dst);
}

// clockwise distance along tile perimeter from 0,0 to point p
static real perimDistCW(vt_point p)
{
  if(p.x == 0) return p.y;
  if(p.y == 1) return 1 + p.x;
  if(p.x == 1) return 2 + (1 - p.y);
  if(p.y == 0) return 3 + (1 - p.x);
  assert(false && "Point not on perimeter!"); return -1;
}

void TileBuilder::buildCoastline()
{
  if(m_coastline.empty()) { return; }
  LOGD("Processing %d coastline segments for tile %s", int(m_coastline.size()), m_id.toString().c_str());
  auto layit = m_layers.find(m_oceanLayer);  //"water");
  if(layit == m_layers.end()) { return; }
  vtzero::layer_builder& layerBuild = layit->second;

  vt_polygon ocean;

  struct vt_point_order {
    bool operator()(const vt_point& lhs, const vt_point& rhs) const {
      return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.y < rhs.y);
    }
  };
  std::map<vt_point, vt_linear_ring, vt_point_order> segments;

  for(auto& way : m_coastline) {
    if(way.back() == way.front())
      ocean.emplace_back(std::move(way));
    else
      segments.emplace(way.front(), std::move(way));
  }

  for(auto ii = segments.begin(); ii != segments.end();) {
    vt_linear_ring& ring = ii->second;
    auto jj = segments.find(ring.back());
    if(jj == segments.end()) { ++ii; }
    else if(jj == ii) {
      ocean.push_back(std::move(ring));
      ii = segments.erase(ii);
    }
    else {
      ring.insert(ring.end(), jj->second.begin(), jj->second.end());
      segments.erase(jj);
      // don't advance ii to repeat w/ new ring.back()
    }
  }

  // for remaining segments, we must add path from exit (end) clockwise along tile edge to entry
  //  (beginning) of next segment
  std::map<real, vt_linear_ring> edgesegs;
  for(auto& seg : segments) {
    real d = perimDistCW(seg.second.front());
    if(d < 0) {
      LOG("Invalid coastline segment for %s", m_id.toString().c_str());
      return;
    }
    edgesegs.emplace(d, std::move(seg.second));
  }

  static vt_point corners[] = {{0,0}, {0,1}, {1,1}, {1,0}};
  for(auto ii = edgesegs.begin(); ii != edgesegs.end();) {
    vt_linear_ring& ring = ii->second;
    real dback = perimDistCW(ring.back());
    if(dback < 0) {
      LOG("Invalid coastline segment for %s", m_id.toString().c_str());
      return;
    }
    auto next = edgesegs.lower_bound(dback);
    if(next == edgesegs.end()) { next = edgesegs.begin(); }

    vt_point dest = next->second.front();
    real dfront = next->first;  //perimDistCW(dest);
    if(dfront < dback) { dfront += 4; }
    int c = std::ceil(dback);
    while(c < dfront) {
      ring.push_back(corners[(c++)%4]);
    }
    if(ii == next) {
      ring.push_back(dest);
      ocean.push_back(std::move(ring));
      ii = edgesegs.erase(ii);
    }
    else {
      ring.insert(ring.end(), next->second.begin(), next->second.end());
      edgesegs.erase(next);
      // don't advance ii to repeat w/ new ring.back()
    }
  }
  assert(edgesegs.empty());

  auto build = std::make_unique<vtzero::polygon_feature_builder>(layerBuild);
  for(auto& ring : ocean) {
    simplify(ring, simplifyThresh);
    tilePts.reserve(ring.size());
    for(auto& p : ring) {
      auto ip = glm::i32vec2(p.x*tileExtent + 0.5f, (1 - p.y)*tileExtent + 0.5f);
      if(tilePts.empty() || ip != tilePts.back())
        tilePts.push_back(ip);
    }
    if(tilePts.size() < 4) {}
    else if(tilePts.back() != tilePts.front()) {
      LOGD("Invalid polygon for %s coastline", m_id.toString().c_str());
    }
    else {
      m_hasGeom = true;
      m_totalPts += tilePts.size();
      build->add_ring_from_container(tilePts);
    }
    tilePts.clear();
  }
  // change to virtual void TileBuilder::processOcean() = 0;
  build->add_property("class", "ocean");
  ++m_totalFeats;
  build->commit();
}

void TileBuilder::addCoastline(Feature& way)
{
  WayCoordinateIterator iter(WayPtr(way.ptr()));
  int n = iter.coordinatesRemaining();
  vt_line_string tempPts;
  tempPts.reserve(n);
  vt_point pmin(REAL_MAX, REAL_MAX), pmax(-REAL_MAX, -REAL_MAX);
  while(n-- > 0) {
    vt_point p = toTileCoord(iter.next());
    tempPts.push_back(p);
    pmin = min(p, pmin);
    pmax = max(p, pmax);
  }

  if(pmin.x > 1 || pmin.y > 1 || pmax.x < 0 || pmax.y < 0) { return; }
  vt_multi_line_string clipPts;
  bool noclip = pmin.x >= 0 && pmin.y >= 0 && pmax.x <= 1 && pmax.y <= 1;
  if(noclip) { clipPts.push_back(std::move(tempPts)); }
  else {
    clipper<0> xclip{0,1};
    clipper<1> yclip{0,1};
    clipPts = yclip(xclip(tempPts));
  }

  m_coastline.insert(m_coastline.end(),
      std::make_move_iterator(clipPts.begin()), std::make_move_iterator(clipPts.end()));
}

// convert to relative tile coord (float 0..1)
vt_point TileBuilder::toTileCoord(Coordinate r) {
  return vt_point(m_scale*(glm::dvec2(r.x, r.y) - m_origin));  // + 0.5);
}

void TileBuilder::buildLine(Feature& way)
{
  vt_line_string tempPts;

  WayCoordinateIterator iter(WayPtr(way.ptr()));
  int n = iter.coordinatesRemaining();
  tempPts.reserve(n);
  vt_point pmin(REAL_MAX, REAL_MAX), pmax(-REAL_MAX, -REAL_MAX);
  while(n-- > 0) {
    vt_point p = toTileCoord(iter.next());
    tempPts.push_back(p);
    pmin = min(p, pmin);
    pmax = max(p, pmax);
  }

  // see if we can skip clipping
  if(pmin.x > 1 || pmin.y > 1 || pmax.x < 0 || pmax.y < 0) { return; }
  vt_multi_line_string clipPts;
  bool noclip = pmin.x >= 0 && pmin.y >= 0 && pmax.x <= 1 && pmax.y <= 1;
  if(noclip) { clipPts.push_back(std::move(tempPts)); }
  else {
    clipper<0> xclip{0,1};
    clipper<1> yclip{0,1};
    clipPts = yclip(xclip(tempPts));
  }

  auto build = static_cast<vtzero::linestring_feature_builder*>(m_build.get());
  for(auto& line : clipPts) {
    simplify(line, simplifyThresh);
    // vtzero throws error on duplicate points
    tilePts.reserve(line.size());
    for(auto& p : line) {
      // MVT origin is upper left (NW), whereas geodesk/mercator origin is lower left (SW)
      auto ip = glm::i32vec2(p.x*tileExtent + 0.5f, (1 - p.y)*tileExtent + 0.5f);
      if(tilePts.empty() || ip != tilePts.back())
        tilePts.push_back(ip);
    }
    if(tilePts.size() > 1) {
      m_hasGeom = true;
      m_totalPts += tilePts.size();
      build->add_linestring_from_container(tilePts);
    }  //else LOG("Why?");
    tilePts.clear();
  }
}

void TileBuilder::buildRing(Feature& feat)
{
  vt_polygon tempPts;
  bool noclip = false;

  if(feat.isWay()) {
    WayCoordinateIterator iter(WayPtr(feat.ptr()));
    int n = iter.coordinatesRemaining();
    tempPts.emplace_back();
    tempPts[0].reserve(n);
    vt_point pmin(REAL_MAX, REAL_MAX), pmax(-REAL_MAX, -REAL_MAX);
    while(n-- > 0) {
      vt_point p = toTileCoord(iter.next());
      tempPts[0].push_back(p);
      pmin = min(p, pmin);
      pmax = max(p, pmax);
    }
    if(pmin.x > 1 || pmin.y > 1 || pmax.x < 0 || pmax.y < 0) { return; }
    noclip = pmin.x >= 0 && pmin.y >= 0 && pmax.x <= 1 && pmax.y <= 1;
  }
  else {
    // also done for computing area - should eliminate this repetition
    Polygonizer polygonizer;
    polygonizer.createRings(feat.store(), RelationPtr(feature().ptr()));
    const Polygonizer::Ring* ring = polygonizer.outerRings();
    bool inner = false;
    while(ring) {
      RingCoordinateIterator iter(ring);
      int n = iter.coordinatesRemaining();
      auto& tempRing = tempPts.emplace_back();
      tempRing.reserve(n);
      vt_point pmin(REAL_MAX, REAL_MAX), pmax(-REAL_MAX, -REAL_MAX);
      while(n-- > 0) {
        vt_point p = toTileCoord(iter.next());
        tempRing.push_back(p);
        pmin = min(p, pmin);
        pmax = max(p, pmax);
      }
      if(pmin.x > 1 || pmin.y > 1 || pmax.x < 0 || pmax.y < 0)
        tempPts.pop_back();
      else
        noclip = noclip && pmin.x >= 0 && pmin.y >= 0 && pmax.x <= 1 && pmax.y <= 1;
      ring = ring->next();
      if(!ring && !inner) { ring = polygonizer.innerRings(); inner = true; }
    }
    if(tempPts.empty()) { return; }
  }

  // see if we can skip clipping
  vt_polygon clipPts;
  if(noclip) { clipPts = std::move(tempPts); }
  else {
    clipper<0> xclip{0,1};
    clipper<1> yclip{0,1};
    clipPts = yclip(xclip(tempPts));
  }

  auto build = static_cast<vtzero::polygon_feature_builder*>(m_build.get());
  for(auto& ring : clipPts) {
    simplify(ring, simplifyThresh);
    tilePts.reserve(ring.size());
    for(auto& p : ring) {
      auto ip = glm::i32vec2(p.x*tileExtent + 0.5f, (1 - p.y)*tileExtent + 0.5f);
      if(tilePts.empty() || ip != tilePts.back())
        tilePts.push_back(ip);
    }
    if(tilePts.size() < 4) {}  // tiny polygons get simplified to two points and discarded ... calculate area instead?
    else if(tilePts.back() != tilePts.front()) {
      LOGD("Invalid polygon for feature %lld", feat.id());
    }
    else {
      m_hasGeom = true;
      m_totalPts += tilePts.size();
      build->add_ring_from_container(tilePts);
    }
    tilePts.clear();
  }
}

void TileBuilder::Layer(const std::string& layer, bool isClosed, bool _centroid) {
  if(m_build && m_hasGeom) {
    ++m_totalFeats;
    m_build->commit();
  }
  m_build.reset();  // have to commit/rollback before creating next builder
  m_hasGeom = false;

  auto it = m_layers.find(layer);
  if(it == m_layers.end()) {
    LOG("Layer not found: %s", layer.c_str());
    return;
  }
  vtzero::layer_builder& layerBuild = it->second;

  if(feature().isNode() || _centroid) {
    auto build = std::make_unique<vtzero::point_feature_builder>(layerBuild);
    //build->set_id(++serial);
    auto p = toTileCoord(_centroid ? feature().centroid() : feature().xy());
    auto ip = glm::i32vec2(p.x*tileExtent + 0.5f, (1 - p.y)*tileExtent + 0.5f);
    m_hasGeom = p.x >= 0 && p.y >= 0 && p.x <= 1 && p.y <= 1;
    if(m_hasGeom) {
      ++m_totalPts;
      build->add_point(ip.x, ip.y);
    }
    m_build = std::move(build);
  }
  else if(feature().isArea()) {
    //if(!isClosed) { LOG("isArea() but not isClosed!"); }
    m_build = std::make_unique<vtzero::polygon_feature_builder>(layerBuild);
    buildRing(feature());
  }
  else {
    //if(isClosed) { LOG("isClosed but not isArea()!"); }
    m_build = std::make_unique<vtzero::linestring_feature_builder>(layerBuild);
    if(feature().isWay())
      buildLine(feature());
    else {  //if(feature().isRelation()) {
    // multi-linestring(?)
      for(Feature child : feature().members()) {
        if(child.isWay()) { buildLine(child); }
      }
    }
  }
}
