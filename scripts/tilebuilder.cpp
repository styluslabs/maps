#include "tilebuilder.h"
#include <geom/polygon/RingCoordinateIterator.h>
#include <geom/polygon/RingBuilder.h>
#include <geom/polygon/Segment.h>

#define MINIZ_GZ_IMPLEMENTATION
#include "ulib/miniz_gzip.h"

// TileBuilder

/* ANSI-C code produced by gperf version 3.1 */
/* Command-line: gperf tags.txt  */
/* Computed positions: -k'1,4,10' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gperf@gnu.org>."
#endif


#define TOTAL_KEYWORDS 78
#define MIN_WORD_LENGTH 3
#define MAX_WORD_LENGTH 19
#define MIN_HASH_VALUE 3
#define MAX_HASH_VALUE 169
/* maximum key range = 167, duplicates = 0 */

unsigned int in_word_set (const char *str, size_t len)
{
  static const char * wordlist[] =
    {
      "", "", "",
      "ele",
      "",
      "lanes",
      "", "",
      "ref",
      "", "",
      "access",
      "railway",
      "", "", "", "",
      "covered",
      "cycleway",
      "type",
      "", "", "",
      "cycleway:left",
      "",
      "route",
      "addr:housenumber",
      "tourism",
      "",
      "cycleway:right",
      "place",
      "ISO3166-1:alpha2",
      "leisure",
      "place:CN",
      "",
      "sport",
      "",
      "aeroway",
      "operator",
      "aerodrome",
      "piste:type",
      "colour",
      "cuisine",
      "protect_class",
      "shop",
      "", "",
      "station",
      "", "",
      "water",
      "",
      "wetland",
      "waterway",
      "iata",
      "", "",
      "intermittent",
      "religion",
      "aerialway",
      "",
      "admin_level",
      "service",
      "",
      "name",
      "",
      "height",
      "name:en",
      "historic",
      "piste:grooming",
      "", "",
      "amenity",
      "building",
      "icao",
      "",
      "oneway",
      "barrier",
      "man_made",
      "mtb:scale",
      "building:levels",
      "tunnel",
      "natural",
      "",
      "archaeological_site",
      "", "",
      "landuse",
      "cycleway:both",
      "sqkm",
      "", "",
      "construction",
      "maxspeed",
      "",
      "min_height",
      "",
      "highway",
      "wikidata",
      "",
      "population",
      "",
      "disused",
      "disputed",
      "golf",
      "wikipedia\"",
      "protection_title",
      "footway",
      "", "", "", "",
      "network",
      "maritime",
      "", "", "",
      "surface",
      "", "", "",
      "trail_visibility",
      "bicycle",
      "", "", "", "", "", "", "", "",
      "piste:difficulty",
      "",
      "boundary",
      "", "", "", "", "", "", "", "", "",
      "building:min_level",
      "", "",
      "meadow",
      "", "", "", "",
      "bridge",
      "", "", "", "", "", "", "", "", "",
      "", "", "", "", "", "", "", "",
      "ford"
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      unsigned int key = hash (str, len);

      if (key <= MAX_HASH_VALUE)
        {
          const char *s = wordlist[key];

          if (*str == *s && !strncmp (str + 1, s + 1, len - 1))
            return key;
        }
    }
  return 0;
}


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

std::string TileBuilder::build(const Features& world, const Features& ocean, bool compress)
{
  auto time0 = std::chrono::steady_clock::now();

  double eps = 0.01/MapProjection::metersPerTileAtZoom(m_id.z);
  m_tileBox = tileBox(m_id, eps);
  Features tileFeats = world(m_tileBox);
  m_tileFeats = &tileFeats;

  int nfeats = 0;
  for(Feature f : tileFeats) {  //for(auto it = tileFeats.begin(); it != tileFeats.end(); ++it) {
    //m_feat = *it;  //new(&m_feat) Feature(*it); ... m_feat.~Feature();
    m_feat = &f;

    //m_tags = f.tags();
    //m_tags.clear();
    //for(Tag tag : f.tags()) {
    //  //if(knownTags.count(tag.key())) { m_tags[tag.key()] = tag.value(); }
    //  m_tags.insert({ tag.key(), tag.value() });
    //}

    m_hasTag.reset();
    for(Tag tag : f.tags()) {
      //auto opt = mph::find<knownTags>(std::string_view(tag.key()).substr(0,16));
      std::string_view k = tag.key();
      unsigned int h = in_word_set(k.data(), k.size());
      if(h > 0) {  //opt.has_value()) {
        m_hasTag.set(h);
        m_tags[h] = tag.value();
      }
    }

    processFeature();
    if(f.isWay() && Find("natural") == "coastline") { addCoastline(f); }
    m_area = NAN;
    ++nfeats;
  }
  m_feat = nullptr;

  // ocean polygons
  if(!m_coastline.empty())
    processFeature();
  else {
    LngLat center = MapProjection::projectedMetersToLngLat(MapProjection::tileCenter(m_id));
    // create all ocean tile if center is inside an ocean polygon
    // looks like there might be a bug in FeatureUtils::isEmpty() used by bool(Features), so do this instead
    Features f = ocean.containingLonLat(center.longitude, center.latitude);
    if(f.begin() != f.end()) { processFeature(); }
  }
  Layer("");  // flush final feature
  m_tileFeats = nullptr;

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
    gzip(in_strm, out_strm, 5);  // level = 5 gives nearly same size as 6 but significantly faster
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

// from ulib/geom.cpp
template<class T>
real polygonArea(const std::vector<T>& points)
{
  real area = 0;
  for(size_t ii = 0, jj = points.size() - 1; ii < points.size(); jj = ii++)
    area += (points[jj].x - points[ii].x)*(points[jj].y + points[ii].y);
  return area/2;
}

template<class T>
bool pointInPolygon(const std::vector<T>& poly, T p)
{
  bool in = false;
  for(size_t i = 0, j = poly.size()-1; i < poly.size(); j = i++) {
    if( ((poly[i].y > p.y) != (poly[j].y > p.y)) &&
        (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) )
      in = !in;
  }
  return in;
}

// convert to relative tile coord (float 0..1)
vt_point TileBuilder::toTileCoord(Coordinate r) {
  return vt_point(m_scale*(glm::dvec2(r.x, r.y) - m_origin));  // + 0.5);
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
  // if m_coastline is empty, we will just create all ocean tile
  LOGD("Processing %d coastline segments for tile %s", int(m_coastline.size()), m_id.toString().c_str());

  vt_multi_polygon outers;
  vt_polygon inners;
  auto add_ring = [&](auto&& ring) {
    // water is on right side of coastline ways, so outer rings are clockwise (area < 0)
    return polygonArea(ring) > 0 ? inners.emplace_back(std::move(ring))
        : outers.emplace_back().emplace_back(std::move(ring));
  };

  struct vt_point_order {
    bool operator()(const vt_point& lhs, const vt_point& rhs) const {
      return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.y < rhs.y);
    }
  };
  std::map<vt_point, vt_linear_ring, vt_point_order> segments;

  for(auto& way : m_coastline) {
    if(way.back() == way.front())
      add_ring(std::move(way));
    else
      segments.emplace(way.front(), std::move(way));
  }

  for(auto ii = segments.begin(); ii != segments.end();) {
    vt_linear_ring& ring = ii->second;
    auto jj = segments.find(ring.back());
    if(jj == segments.end()) { ++ii; }
    else if(jj == ii) {
      add_ring(std::move(ring));
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
      add_ring(std::move(ring));
      ii = edgesegs.erase(ii);
    }
    else {
      ring.insert(ring.end(), next->second.begin(), next->second.end());
      edgesegs.erase(next);
      // don't advance ii to repeat w/ new ring.back()
    }
  }
  assert(edgesegs.empty());

  // next, we have to assign inner rings to outer rings
  if(outers.empty()) {
    outers.push_back({{{0,0}, {0,1}, {1,1}, {1,0}, {0,0}}});  // island!
  }
  if(outers.size() == 1) {
    outers[0].insert(outers[0].end(),
        std::make_move_iterator(inners.begin()), std::make_move_iterator(inners.end()));
  }
  else {
    for(vt_linear_ring& inner : inners) {
      // find point not on edge to reduce chance of numerical issues (since outer likely includes edge)
      vt_point pin = inner.front();
      for(auto& p : inner) {
        if(p.x != 0 && p.y != 0 && p.x != 1 && p.y != 1) { pin = p; break; }
      }
      for(vt_polygon& outer : outers) {
        // test if first point of inner is inside outer ring
        if(pointInPolygon(outer.front(), pin)) {
          outer.emplace_back(std::move(inner));
          break;
        }
      }
    }
  }

  // MVT polygon is single CCW outer ring followed by 0 or more CW inner rings; multipolygon repeats this
  auto build = static_cast<vtzero::polygon_feature_builder*>(m_build.get());
  for(vt_polygon& outer : outers) {
    for(vt_linear_ring& ring : outer) {
      simplify(ring, simplifyThresh);
      tilePts.reserve(ring.size());
      // flipping y will flip the winding direction, as desired for MVT convention of CCW outer
      for(auto it = ring.begin(); it != ring.end(); ++it) {
        auto ip = glm::i32vec2(it->x*tileExtent + 0.5f, (1 - it->y)*tileExtent + 0.5f);
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
  }
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

template<class T>
void TileBuilder::addRing(vt_polygon& poly, T&& iter)
{
  int n = iter.coordinatesRemaining();
  vt_linear_ring& ring = poly.emplace_back();
  ring.reserve(n);
  vt_point pmin(REAL_MAX, REAL_MAX), pmax(-REAL_MAX, -REAL_MAX);
  while(n-- > 0) {
    vt_point p = toTileCoord(iter.next());
    ring.push_back(p);
    pmin = min(p, pmin);
    pmax = max(p, pmax);
  }
  if(pmin.x > 1 || pmin.y > 1 || pmax.x < 0 || pmax.y < 0) { poly.pop_back(); }
  else if(pmin.x < 0 || pmin.y < 0 || pmax.x > 1 || pmax.y > 1) {
    clipper<0> xclip{0,1};
    clipper<1> yclip{0,1};
    ring = yclip(xclip(ring));
  }
}

template<class B, class It>
static void set_points(B& build, It _begin, It _end)
{
  for(It it = _begin; it != _end; ++it)
    build.set_point(*it);
}

void TileBuilder::buildPolygon(Feature& feat)
{
  vt_multi_polygon tempMP;

  if(feat.isWay()) {
    addRing(tempMP.emplace_back(), WayCoordinateIterator(WayPtr(feat.ptr())));
  }
  else {
    // also done for computing area - should eliminate this repetition
    Polygonizer polygonizer;
    polygonizer.createRings(feat.store(), RelationPtr(feature().ptr()));
    polygonizer.assignAndMergeHoles();
    const Polygonizer::Ring* outer = polygonizer.outerRings();
    while(outer) {
      vt_polygon& tempPoly = tempMP.emplace_back();
      addRing(tempPoly, RingCoordinateIterator(outer));
      const Polygonizer::Ring* inner = outer->firstInner();
      while(inner) {
        addRing(tempPoly, RingCoordinateIterator(inner));
        inner = inner->next();
      }
      outer = outer->next();
    }
  }

  // Tangram mvt.cpp fixes the winding direction for outer ring from the first polygon in the tile, rather
  //  than using the MVT spec of positive signed area or using the winding of the first ring of each
  //  multipolygon; in any case, we should just follow the spec

  auto build = static_cast<vtzero::polygon_feature_builder*>(m_build.get());
  for(vt_polygon& outer : tempMP) {
    bool isouter = true;
    for(vt_linear_ring& ring : outer) {
      simplify(ring, simplifyThresh);
      tilePts.reserve(ring.size());
      // note that flipping y will flip the winding direction
      for(auto& p : ring) {
        auto ip = glm::i32vec2(p.x*tileExtent + 0.5f, (1 - p.y)*tileExtent + 0.5f);
        if(tilePts.empty() || ip != tilePts.back())
          tilePts.push_back(ip);
      }
      // tiny polygons get simplified to two points and discarded ... calculate area instead?
      if(tilePts.size() < 4) {}
      else if(tilePts.back() != tilePts.front()) {
        LOGD("Invalid polygon for feature %lld", feat.id());
      }
      else {
        m_hasGeom = true;
        m_totalPts += tilePts.size();
        build->add_ring(tilePts.size());
        if((polygonArea(ring) < 0) != isouter)
          set_points(*build, tilePts.rbegin(), tilePts.rend());
        else
          set_points(*build, tilePts.begin(), tilePts.end());
      }
      isouter = false;  // any additional rings in this polygon are inner rings
      tilePts.clear();
    }
  }
}

//std::string TileBuilder::Find(const std::string& key) { return feature()[key]; }
//std::string TileBuilder::Find(const std::string& key)
//{
//  //if(!knownTags.count(key)) { LOG("Unknown tag %s", key.c_str()); }
//  auto it = m_tags.find(key);
//  return it != m_tags.end() ? std::string(it->second) : std::string();
//}

//GetParents() return m_tileFeats->relations().parentsOf(feature()); -- crashes since parent iteration not implemented
Features TileBuilder::GetMembers()
{
  return m_tileFeats->membersOf(feature());
}

void TileBuilder::Layer(const std::string& layer, bool isClosed, bool _centroid)
{
  if(m_build && m_hasGeom) {
    ++m_totalFeats;
    m_build->commit();
  }
  m_build.reset();  // have to commit/rollback before creating next builder
  m_hasGeom = false;

  if(layer.empty()) { return; }  // layer == "" to flush last feature
  auto it = m_layers.find(layer);
  if(it == m_layers.end()) {
    LOG("Layer not found: %s", layer.c_str());
    return;
  }
  vtzero::layer_builder& layerBuild = it->second;

  // ocean
  if(!m_feat) {
    m_build = std::make_unique<vtzero::polygon_feature_builder>(layerBuild);
    buildCoastline();
  }
  else if(feature().isNode() || _centroid) {
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
    buildPolygon(feature());
  }
  else {
    //if(isClosed) { LOG("isClosed but not isArea()!"); }
    m_build = std::make_unique<vtzero::linestring_feature_builder>(layerBuild);
    if(feature().isWay())
      buildLine(feature());
    else {  //if(feature().isRelation()) {
      // multi-linestring(?)
      for(Feature child : feature().members()) {
        if(child.isWay() && m_tileBox.intersects(child.bounds())) {
          buildLine(child);
        }
      }
    }
  }
}
