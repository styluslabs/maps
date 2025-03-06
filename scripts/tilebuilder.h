#pragma once

#include <geodesk/geodesk.h>
#include <vtzero/builder.hpp>
#include "util/mapProjection.h"
#include "clipper.h"

#include <bitset>
//#include "mph"

inline constexpr unsigned int hash (const char *str, size_t len)
{
  constexpr unsigned char asso_values[] =
    {
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170,   5, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170,   5, 170, 170, 170, 170, 170, 170,  10, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170,   0, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170,  10, 170,   5,  65,  10,
       80,   0,  85,  15,  45,  45, 170, 170,   0,  60,
       60,  25,  15, 170,   5,  25,  15,  15,  30,  45,
      170,  50, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170, 170, 170, 170, 170,
      170, 170, 170, 170, 170, 170
    };
  unsigned int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)str[9]];
      /*FALLTHROUGH*/
      case 9:
      case 8:
      case 7:
      case 6:
      case 5:
      case 4:
        hval += asso_values[(unsigned char)str[3]];
      /*FALLTHROUGH*/
      case 3:
      case 2:
      case 1:
        hval += asso_values[(unsigned char)str[0]];
        break;
    }
  return hval;
}

consteval unsigned int TAG(std::string_view s) { return hash(s.data(), s.size()); }
constexpr unsigned int runtimeTag(std::string_view s) { return hash(s.data(), s.size()); }

#define Find(s) readTag(TAG(s))
#define Holds(s) (readTag(TAG(s)) != "")


using geodesk::Feature;
using geodesk::Features;
using geodesk::Mercator;
using geodesk::Coordinate;

using Tangram::LngLat;
using Tangram::TileID;
using Tangram::MapProjection;

#define LOG(fmt, ...) fprintf(stderr, fmt "\n", ## __VA_ARGS__)
#ifdef NDEBUG
#define LOGD(...) do {} while(0)
#else
#define LOGD LOG
#endif

class TileBuilder
{
public:
  geodesk::Box m_tileBox;
  Features* m_tileFeats = nullptr;
  Feature* m_feat = nullptr;  //std::reference_wrapper<Feature> m_feat;
  std::unique_ptr<vtzero::feature_builder> m_build;
  double m_area = NAN;

  std::bitset<170> m_hasTag;
  std::array<geodesk::TagValue, 170> m_tags;

  // coord mapping
  glm::dvec2 m_origin;
  double m_scale = 0;
  const float tileExtent = 4096;  // default vtzero extent
  float simplifyThresh = 1/512.0f;

  // stats
  int m_totalPts = 0;
  int m_totalFeats = 0;
  bool m_hasGeom = false;  // doesn't seem we can get this from vtzero

  // temp containers
  std::vector<glm::i32vec2> tilePts;

  // coastline
  vt_multi_line_string m_coastline;

  TileID m_id;
  vtzero::tile_builder m_tile;
  std::map<std::string, vtzero::layer_builder> m_layers;

  TileBuilder(TileID _id, const std::vector<std::string>& layers);
  Feature& feature() { return *m_feat; }
  glm::vec2 toTileCoord(Coordinate r);
  virtual void processFeature() = 0;
  std::string build(const Features& world, const Features& ocean, bool compress = true);

  // reading geodesk feature
  //std::string Find(const std::string& key);  // { return feature()[key]; }
  std::string readTag(unsigned int idx) { return m_hasTag[idx] ? std::string(m_tags[idx]) : std::string(); }
  std::string Id() { return std::to_string(feature().id()); }
  //bool Holds(const std::string& key) { return Find(key) != ""; }
  bool IsClosed() { return feature().isArea(); }
  double Length() { return feature().length(); }
  double Area() { if(std::isnan(m_area)) { m_area = feature().area(); }  return m_area; }
  //double AreaIntersecting();
  Features GetMembers();

  // writing tile feature
  bool MinZoom(int z) { return m_id.z >= z; }
  void Attribute(const std::string& key, const std::string& val) {
    if(!val.empty()) { m_build->add_property(key, val); }  //&& m_id.z >= z
  }
  //void Attribute(const std::string& key, const TagValue& val, int z = 0) { Attribute(key, std::string(val), z); }
  //void Attribute(const std::string& key) { Attribute(key, Find(key)); }
  void AttributeNumeric(const std::string& key, double val) { m_build->add_property(key, val); }
  //void ZOrder(float order) { /* Not supported - not needed since Tangram handles ordering */ }
  void Layer(const std::string& layer, bool isClosed = false, bool _centroid = false);
  void LayerAsCentroid(const std::string& layer) { Layer(layer, false, true); }

//private:
  void buildLine(Feature& way);
  void buildPolygon(Feature& way);
  template<class T> void addRing(vt_polygon& poly, T&& iter);

  void addCoastline(Feature& way);
  void buildCoastline();
};
