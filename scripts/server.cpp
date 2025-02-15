// Serve tiles from multiple mbtiles files, generating missing tiles on demand

#include <geodesk/geodesk.h>

using namespace geodesk;

#include <vtzero/builder.hpp>
#include <vtzero/index.hpp>

#include <map>
//#define CPPHTTPLIB_OPENSSL_SUPPORT
//#define CPPHTTPLIB_ZLIB_SUPPORT
#include "httplib.h"
#include "sqlitepp.h"
#include "ulib/threadutil.h"
#include "util/mapProjection.h"

#define STRINGUTIL_IMPLEMENTATION
#include "ulib/stringutil.h"

#define MINIZ_GZ_IMPLEMENTATION
#include "ulib/miniz_gzip.h"


using Tangram::LngLat;
using Tangram::TileID;
using Tangram::MapProjection;

#define LOG(...) fprintf(stderr, __VA_ARGS__)
#ifdef NDEBUG
#define LOGD(...)
#else
#define LOGD LOG
#endif

static LngLat tileCoordToLngLat(const TileID& tileId, const glm::vec2& tileCoord)
{
  using namespace Tangram;
  double scale = MapProjection::metersPerTileAtZoom(tileId.z);
  ProjectedMeters tileOrigin = MapProjection::tileSouthWestCorner(tileId);
  ProjectedMeters meters = glm::dvec2(tileCoord) * scale + tileOrigin;
  return MapProjection::projectedMetersToLngLat(meters);
}

static std::string tileBBox(const TileID& id, double eps)
{
  LngLat minBBox = tileCoordToLngLat(id, {eps, eps});
  LngLat maxBBox = tileCoordToLngLat(id, {1-eps, 1-eps});
  return fstring("%.9f,%.9f,%.9f,%.9f", minBBox.longitude, minBBox.latitude, maxBBox.longitude, maxBBox.latitude);
}

// ref for osmium extract: https://github.com/lambdajack/sequentially-generate-planet-mbtiles/blob/master/internal/extract/extract.go
static std::string getBuildCmd(const TileID& id, const char* idstr)
{
  double eps = 0.01/MapProjection::metersPerTileAtZoom(id.z);
  std::string slicebox = tileBBox(id, -eps);
  std::string tilebox = tileBBox(id, eps);
  return fstring("osmium extract -b %s --set-bounds -o %s_extract.osm.pbf planet.osm.pbf && "  //--overwrite
      "build/tilemaker --store store --config config-14.json --bbox %s --input %s_extract.osm.pbf --output %s.mbtiles && "
      "rm %s_extract.osm.pbf",
      slicebox.c_str(), idstr, tilebox.c_str(), idstr, idstr, idstr);
}

class TileDB : public SQLiteDB
{
public:
  SQLiteStmt getTile = {NULL};
};

int old_main(int argc, char* argv[])
{
  struct Stats_t { size_t reqs = 0, reqsok = 0, bytesout = 0; } stats;
  const char* worldDBPath = argc > 1 ? argv[1] : "planet.mbtiles";
  static const int BLKZ = 8;
  static const char* getTileSQL =
      "SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;";

  TileDB worldDB;
  std::map<std::string, TileDB> openDBs;
  ThreadPool buildWorkers(1);  // ThreadPool(1) is like AsyncWorker
  httplib::Server svr;  //httplib::SSLServer svr;
  auto time0 = std::chrono::steady_clock::now();

  if(worldDB.open(worldDBPath, SQLITE_OPEN_READONLY) != SQLITE_OK) {
    LOG("Error opening world mbtiles %s\n", worldDBPath);
    return -1;
  }
  worldDB.getTile = worldDB.stmt(getTileSQL);

  svr.Get("/status", [&](const httplib::Request& req, httplib::Response& res) {
    auto t1 = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(t1 - time0).count();
    auto statstr = fstring("Uptime: %.0f s\nReqs: %llu\n200 Reqs: %llu\nBytes out: %llu\n",
        dt, stats.reqs, stats.reqsok, stats.bytesout);
    res.set_content(statstr, "text/plain");
    return httplib::StatusCode::OK_200;
  });

  svr.Get("/tiles/:z/:x/:y", [&](const httplib::Request& req, httplib::Response& res) {
    LOGD("Request %s\n", req.path.c_str());
    ++stats.reqs;
    const char* zstr = req.path_params.at("z").c_str();
    const char* xstr = req.path_params.at("x").c_str();
    const char* ystr = req.path_params.at("y").c_str();
    char* zout, *xout, *yout;
    int z = std::strtol(zstr, &zout, 10);
    int x = std::strtol(xstr, &xout, 10);
    int y = std::strtol(ystr, &yout, 10);
    if(zout == zstr || xout == xstr || ystr == yout ||
        z < 0 || x < 0 || y < 0 || x >= (1 << z) || y >= (1 << z)) {
      return httplib::StatusCode::BadRequest_400;
    }

    int ydb = (1 << z) - 1 - y;

    if(z > 14)
      return httplib::StatusCode::NotFound_404;

    //if(z <= 12) {
      worldDB.stmt(getTileSQL).bind(z, x, ydb).exec([&](sqlite3_stmt* stmt){
        const char* blob = (const char*) sqlite3_column_blob(stmt, 0);
        const int length = sqlite3_column_bytes(stmt, 0);
        res.set_content(blob, length, "application/vnd.mapbox-vector-tile");
      });

      if(res.body.empty()) {
        return httplib::StatusCode::NotFound_404;
      }

    /*}
    else {
      // states:
      // - not requested: no entry in openDBs
      // - building tiles: entry in openDBs w/ no db set; continue to return 404
      // - building tiles failed: same as building
      // - building tiles done: db set for entry in openDBs

      int blkx = x >> (z - BLKZ), blky = y >> (z - BLKZ);
      auto blkdir = std::to_string(BLKZ) + "/" + std::to_string(blkx) + "/";
      auto blkid = blkdir + std::to_string(blky);
      auto blkit = openDBs.find(blkid);
      if(blkit == openDBs.end()) {
        // first request for tile in this block
        SQLiteDB& db = openDBs[blkid];  // create openDBs entry
        std::string dbfile = blkid + ".mbtiles";
        if(db.open(dbfile, SQLITE_OPEN_READONLY) != SQLITE_OK) {
          // no mbtiles for this block
          bool hasBlock = false;
          worldDB.getTile.bind(BLKZ, blkx, (1 << BLKZ) - 1 - blky).exec([&](sqlite3_stmt* stmt){
            const int length = sqlite3_column_bytes(stmt, 0);
            if(length > 75) { hasBlock = true; }
          });
          if(!hasBlock)
            LOGD("Block tile %d/%d/%d is missing or empty, skipping build for %d/%d/%d\n", BLKZ, blkx, blky, z, x, y);
          else {
            auto buildfn = [=, &db]() {
              if(system(("mkdir -p " + blkdir).c_str()) != 0) {
                LOG("Error creating path %s\n", blkdir.c_str());
                return;
              }
              std::string cmd = getBuildCmd(TileID(blkx, blky, BLKZ), blkid.c_str());
              LOGD("Running: %s\n", cmd.c_str());
              FILE* pipe = popen(cmd.c_str(), "r");
              if(!pipe) { LOG("Error running %s\n", cmd.c_str()); return; }
              std::array<char, 128> buffer;
              std::string result;
              while(fgets(buffer.data(), int(buffer.size()), pipe)) {
                LOGD(buffer.data());
                result += buffer.data();
              }
              int res = pclose(pipe);
              if(res != 0)
                LOG("tilemaker failed with error code %d:\n\n%s\n", res, result.c_str());
              else if(db.open(dbfile, SQLITE_OPEN_READONLY) != SQLITE_OK)
                LOG("Error opening %s\n", dbfile.c_str());
            };
            buildWorkers.enqueue(buildfn);
          }
          return httplib::StatusCode::NotFound_404;  // build in progress (if non-empty)
        }
      }
      TileDB& tileDB = blkit->second;
      if(!tileDB.db)
        return httplib::StatusCode::NotFound_404;  // build in progress or failed
      // tile is available!
      if(!tileDB.getTile.stmt)
        tileDB.getTile = tileDB.stmt(getTileSQL);
      tileDB.getTile.bind(z, x, ydb).exec([&](sqlite3_stmt* stmt){
        const char* blob = (const char*) sqlite3_column_blob(stmt, 0);
        const int length = sqlite3_column_bytes(stmt, 0);
        res.set_content(blob, length, "application/vnd.mapbox-vector-tile");
      });
    } */

    LOGD("Serving %s\n", req.path.c_str());
    ++stats.reqsok;
    stats.bytesout += res.body.size();
    res.set_header("Content-Encoding", "gzip");
    return httplib::StatusCode::OK_200;
  });

  LOG("Server listening on 8080");
  svr.listen("0.0.0.0", 8080);
  return 0;
}


static Box tileBox(const TileID& id, double eps)
{
  LngLat minBBox = tileCoordToLngLat(id, {eps, eps});
  LngLat maxBBox = tileCoordToLngLat(id, {1-eps, 1-eps});
  return Box::ofWSEN(minBBox.longitude, minBBox.latitude, maxBBox.longitude, maxBBox.latitude);
}

/*
currObj = nil
if not Attribute then
  --print("Detected Tilemaker 2.4 - creating thunks") -- printed many times
  Attribute = function(...) currObj:Attribute(...); end
  AttributeNumeric = function(...) currObj:AttributeNumeric(...); end
  MinZoom = function(...) currObj:MinZoom(...); end
  ZOrder = function(...) currObj:ZOrder(...); end
  Layer = function(...) currObj:Layer(...); end
  LayerAsCentroid = function(...) currObj:LayerAsCentroid(...); end
  NextRelation = function(...) currObj:NextRelation(...); end
  Accept = function(...) currObj:Accept(...); end

  Find = function(...) return currObj:Find(...); end
  FindInRelation = function(...) return currObj:FindInRelation(...); end
  Id = function(...) return currObj:Id(...); end
  Holds = function(...) return currObj:Holds(...); end
  IsClosed = function(...) return currObj:IsClosed(...); end
  Length = function(...) return currObj:Length(...); end
  Area = function(...) return currObj:Area(...); end
  AreaIntersecting = function(...) return currObj:AreaIntersecting(...); end
end
*/

//MinZoom(z) -> if(!MinZoom(z)) { return; }

struct TileBuilder
{
  Feature* m_feat = nullptr;  //std::reference_wrapper<Feature> m_feat;
  std::unique_ptr<vtzero::feature_builder> m_build;
  double m_area = NAN;

  TileID m_id;
  vtzero::tile_builder m_tile;
  std::map<std::string, vtzero::layer_builder> m_layers;

  TileBuilder(TileID _id, const std::vector<std::string>& layers) : m_id(_id) {
    for(auto& l : layers)
      m_layers.emplace(l, m_tile, l);
  }

  virtual void processFeature() = 0;

  std::string build(const Features& world) {
    double eps = 0.01/MapProjection::metersPerTileAtZoom(m_id.z);
    Box bbox = tileBox(m_id, eps);
    Features tileFeats = world(bbox);

    //int nfeats = 0;
    for(Feature f : tileFeats) {  //for(auto it = tileFeats.begin(); it != tileFeats.end(); ++it) {
      //m_feat = *it;  //new(&m_feat) Feature(*it); ... m_feat.~Feature();
      m_feat = &f;
      processFeature();
      m_area = NAN;
      //++nfeats;
    }
    m_feat = nullptr;
    return serialize();
  }

  Feature& feature() { return *m_feat; }

  std::string serialize(bool compress = true) {
    if(m_build)
      m_build->commit();
    m_build.reset();
    std::string mvt = m_tile.serialize();
    if (!compress) { return mvt; }
    std::stringstream in_strm(mvt);
    std::stringstream out_strm;
    gzip(in_strm, out_strm);
    return out_strm.str();
  }

  // reading geodesk feature
  std::string Find(const std::string& key) { return feature()[key]; }
  std::string Id() { return std::to_string(feature().id()); }
  bool Holds(const std::string& key) { return !Find(key).empty(); }
  bool IsClosed() { return feature().isArea(); }
  double Length() { return feature().length(); }
  double Area() { if(std::isnan(m_area)) { m_area = feature().area(); }  return m_area; }
  double AreaIntersecting();
  //std::string FindInRelation(Relation rel, const std::string& key) { return rel[key]; }

  // writing tile feature
  bool MinZoom(int z) { return m_id.z >= z; }
  void Attribute(const std::string& key, const std::string& val, int z = 0) {
    if(!val.empty() && m_id.z >= z) { m_build->add_property(key, val); }
  }
  void Attribute(const std::string& key, int z = 0) { Attribute(key, Find(key), z); }
  void AttributeNumeric(const std::string& key, double val) { m_build->add_property(key, val); }
  void ZOrder(float order) { /* Not supported - not needed since Tangram handles ordering */ }

  void Layer(const std::string& layer, bool isClosed = false, bool _centroid = false) {
    if(m_build)
      m_build->commit();

    auto it = m_layers.find(layer);
    if(it == m_layers.end()) {
      LOG("Layer not found: %s", layer.c_str());
      return;
    }
    vtzero::layer_builder& layerBuild = it->second;

    if(feature().isNode() || _centroid) {
      auto build = std::make_unique<vtzero::point_feature_builder>(layerBuild);
      //build->set_id(++serial);
      auto xy = _centroid ? feature().centroid() : feature().xy();
      build->add_point(xy.x, xy.y);
      m_build = std::move(build);
    }
    else if(feature().isArea()) {
      if(!isClosed) { LOG("isArea() but not isClosed!"); }
      auto build = std::make_unique<vtzero::polygon_feature_builder>(layerBuild);

      if(feature().isWay()) {
        WayCoordinateIterator iter(WayPtr(feature().ptr()));
        int n = iter.coordinatesRemaining();
        build->add_ring(n);
        while(n-- > 0) {
          auto p = iter.next();
          build->set_point(p.x, p.y);
        }
        //feature.close_ring(); ???
      }
      else {
        for(Feature child : feature().members()) {
          // OSM multipolygons cannot be nested, so this should be OK
          if(!child.isWay()) { continue; }
          WayCoordinateIterator iter(WayPtr(feature().ptr()));
          int n = iter.coordinatesRemaining();
          build->add_ring(n);
          while(n-- > 0) {
            auto p = iter.next();
            build->set_point(p.x, p.y);
          }
          //feature.close_ring(); ???
        }
      }

      m_build = std::move(build);
    }
    else if(feature().isWay()) {
      if(isClosed) { LOG("isClosed but not isArea()!"); }
      auto build = std::make_unique<vtzero::linestring_feature_builder>(layerBuild);
      WayCoordinateIterator iter(WayPtr(feature().ptr()));
      int n = iter.coordinatesRemaining();
      build->add_linestring(n);
      while(n-- > 0) {
        auto p = iter.next();
        build->set_point(p.x, p.y);
      }
      m_build = std::move(build);
    }
    else if(feature().isRelation()) {
      // multi-linestring(?)
      auto build = std::make_unique<vtzero::linestring_feature_builder>(layerBuild);
      for(Feature child : feature().members()) {
        if(!child.isWay()) { continue; }
        WayCoordinateIterator iter(WayPtr(feature().ptr()));
        int n = iter.coordinatesRemaining();
         build->add_linestring(n);
        while(n-- > 0) {
          auto p = iter.next();
          build->set_point(p.x, p.y);
        }
      }
    }
  }

  void LayerAsCentroid(const std::string& layer) { Layer(layer, false, true); }

};

int main(int argc, char* argv[])
{
  if(argc < 2) {
    LOG("No gol file specified!");
    return -1;
  }

  Features world(argv[1]);

  LOG("Loaded %s", argv[1]);

  TileID id(2619, 6332, 14);  // Alamo square!

  // pass id and world to builder and let it get features? ... in the future it could limit query based on zoom level
  AscendTileBuilder tileBuilder(id);

  auto time0 = std::chrono::steady_clock::now();

  double eps = 0.01/MapProjection::metersPerTileAtZoom(id.z);
  Box bbox = tileBox(id, eps);
  Features tileFeats = world(bbox);

  //Feature zurich = features("a[boundary=administrative][admin_level=8][name:en=Zurich]").one();
  //Features pubs = features("na[amenity=pub]");
  //pubs.within(zurich)

  int nfeats = 0;
  for(Feature f : tileFeats) {
    if(f["name"] == "Alamo Square")
      LOG("Found it");
    ++nfeats;
  }

  auto t1 = std::chrono::steady_clock::now();
  double dt = std::chrono::duration<double>(t1 - time0).count();
  LOG("Counted %d features in %f ms", nfeats, dt*1000);
  return 0;
}