#pragma once

#include <geodesk/geodesk.h>

#include <vtzero/builder.hpp>
//#include <vtzero/index.hpp>

#include "util/mapProjection.h"

using geodesk::Feature;
using geodesk::Features;
using geodesk::Mercator;
using geodesk::Coordinate;

using Tangram::LngLat;
using Tangram::TileID;
using Tangram::MapProjection;

#define LOG(fmt, ...) fprintf(stderr, fmt "\n", ## __VA_ARGS__)
#ifdef NDEBUG
#define LOGD(...)
#else
#define LOGD LOG
#endif

class TileBuilder
{
public:
  Feature* m_feat = nullptr;  //std::reference_wrapper<Feature> m_feat;
  std::unique_ptr<vtzero::feature_builder> m_build;
  double m_area = NAN;

  // coord mapping
  glm::dvec2 m_origin;
  double m_scale = 0;
  const float tileExtent = 4096;  // default vtzero extent
  const float simplifyThresh = 1/512.0f;

  TileID m_id;
  vtzero::tile_builder m_tile;
  std::map<std::string, vtzero::layer_builder> m_layers;

  TileBuilder(TileID _id, const std::vector<std::string>& layers);
  Feature& feature() { return *m_feat; }
  glm::vec2 toTileCoord(Coordinate r);
  virtual void processFeature() = 0;
  std::string build(const Features& world, bool compress = true);

  // reading geodesk feature
  std::string Find(const std::string& key) { return feature()[key]; }
  std::string Id() { return std::to_string(feature().id()); }
  bool Holds(const std::string& key) { return Find(key).empty(); }
  bool IsClosed() { return feature().isArea(); }
  double Length() { return feature().length(); }
  double Area() { if(std::isnan(m_area)) { m_area = feature().area(); }  return m_area; }
  //double AreaIntersecting();

  // writing tile feature
  bool MinZoom(int z) { return m_id.z >= z; }
  void Attribute(const std::string& key, const std::string& val) {
    if(!val.empty()) { m_build->add_property(key, val); }  //&& m_id.z >= z
  }
  //void Attribute(const std::string& key, const TagValue& val, int z = 0) { Attribute(key, std::string(val), z); }
  void Attribute(const std::string& key) { Attribute(key, Find(key)); }
  void AttributeNumeric(const std::string& key, double val) { m_build->add_property(key, val); }
  //void ZOrder(float order) { /* Not supported - not needed since Tangram handles ordering */ }
  void Layer(const std::string& layer, bool isClosed = false, bool _centroid = false);
  void LayerAsCentroid(const std::string& layer) { Layer(layer, false, true); }
};
