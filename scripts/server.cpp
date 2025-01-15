// Serve tiles from multiple mbtiles files, generating missing tiles on demand

#include <map>
//#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "sqlitepp.h"
#include "ulib/threadutil.h"
#include "util/mapProjection.h"

#define STRINGUTIL_IMPLEMENTATION
#include "ulib/stringutil.h"


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

class TileDB : public SQLiteDB
{
public:
  SQLiteStmt getTile = {NULL};
};

int main(int argc, char* argv[])
{
  const char* worldDBPath = "planet12.mbtiles";
  static std::string buildCmd = "build/tilemaker --store store --config config-14.json --input AlpsEast.osm.pbf";
  static const int BLKZ = 10;
  static const char* getTileSQL =
      "SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;";

  TileDB worldDB;
  std::map<std::string, TileDB> openDBs;
  ThreadPool buildWorkers(1);  // ThreadPool(1) is like AsyncWorker
  httplib::Server svr;  //httplib::SSLServer svr;

  if(worldDB.open(worldDBPath, SQLITE_OPEN_READONLY) != SQLITE_OK) {
    LOG("Error opening world mbtiles %s\n", worldDBPath);
    return -1;
  }
  worldDB.getTile = worldDB.stmt(getTileSQL);

  svr.Get("/tiles/:z/:x/:y", [&](const httplib::Request& req, httplib::Response& res) {
    LOGD("Request %s\n", req.path.c_str());
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

    if(z <= 12) {
      worldDB.stmt(getTileSQL).bind(z, x, ydb).exec([&](sqlite3_stmt* stmt){
        const char* blob = (const char*) sqlite3_column_blob(stmt, 0);
        const int length = sqlite3_column_bytes(stmt, 0);
        res.set_content(blob, length, "application/vnd.mapbox-vector-tile");
      });
    }
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
              double eps = 0.1/MapProjection::metersPerTileAtZoom(BLKZ);
              LngLat minBBox = tileCoordToLngLat(TileID(blkx, blky, BLKZ), {eps, eps});
              LngLat maxBBox = tileCoordToLngLat(TileID(blkx, blky, BLKZ), {1-eps, 1-eps});
              std::string cmd = buildCmd + fstring(" --bbox %.9f %.9f %.9f %.9f --output %s",
                  minBBox.longitude, minBBox.latitude, maxBBox.longitude, maxBBox.latitude, dbfile.c_str());
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
    }

    LOGD("Serving %s\n", req.path.c_str());
    res.set_header("Content-Encoding", "gzip");
    return httplib::StatusCode::OK_200;
  });

  svr.listen("0.0.0.0", 8080);
}