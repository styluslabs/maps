// Serve tiles from multiple mbtiles files, generating missing tiles on demand

#include <map>
#include "tilebuilder.h"
#include "sqlitepp.h"
#include "ulib/threadutil.h"
//#include "util/mapProjection.h"

#define STRINGUTIL_IMPLEMENTATION
#include "ulib/stringutil.h"

// httplib should be last include because it pulls in, e.g., fcntl.h with #defines that break geodesk headers
//#define CPPHTTPLIB_OPENSSL_SUPPORT
//#define CPPHTTPLIB_ZLIB_SUPPORT
#include "httplib.h"

// using Tangram::LngLat;
// using Tangram::TileID;
// using Tangram::MapProjection;

// #define LOG(...) fprintf(stderr, __VA_ARGS__)
// #ifdef NDEBUG
// #define LOGD(...)
// #else
// #define LOGD LOG
// #endif

/*static LngLat tileCoordToLngLat(const TileID& tileId, const glm::vec2& tileCoord)
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
}*/

extern std::string buildTile(Features& world, TileID id);

class TileDB : public SQLiteDB
{
public:
  SQLiteStmt getTile = {NULL};
  SQLiteStmt putTile = {NULL};
};

int main(int argc, char* argv[])
{
  struct Stats_t { size_t reqs = 0, reqsok = 0, bytesout = 0; } stats;
  const char* worldDBPath = argc > 2 ? argv[2] : "planet.mbtiles";
  //static const int BLKZ = 8;
  static const char* schemaSQL = R"(BEGIN;
    CREATE TABLE IF NOT EXISTS tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob);
    CREATE UNIQUE INDEX IF NOT EXISTS tile_index on tiles (zoom_level, tile_column, tile_row);
  COMMIT;)";
  static const char* getTileSQL =
      "SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;";
  static const char* putTileSQL =
      "INSERT INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?,?,?,?);";

  if(argc < 2) {
    LOG("No gol file specified!");
    return -1;
  }
  Features worldGOL(argv[1]);
  LOG("Loaded %s", argv[1]);

  TileDB worldDB;
  //std::map<std::string, TileDB> openDBs;
  // ... separate queues for high zoom and low zoom (slower build)?
  std::mutex buildMutex;
  std::map< TileID, std::shared_future<std::string> > buildQueue;
  ThreadPool buildWorkers(1);  // ThreadPool(1) is like AsyncWorker
  httplib::Server svr;  //httplib::SSLServer svr;
  auto time0 = std::chrono::steady_clock::now();

  if(worldDB.open(worldDBPath, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) != SQLITE_OK) {  //SQLITE_OPEN_READONLY
    LOG("Error opening world mbtiles %s\n", worldDBPath);
    return -1;
  }
  worldDB.exec(schemaSQL);
  worldDB.getTile = worldDB.stmt(getTileSQL);
  worldDB.putTile = worldDB.stmt(putTileSQL);

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

    if(z > 14) { return httplib::StatusCode::NotFound_404; }

    TileID id(x, y, z);
    int ydb = (1 << z) - 1 - y;

    worldDB.getTile.bind(z, x, ydb).exec([&](sqlite3_stmt* stmt){
      const char* blob = (const char*) sqlite3_column_blob(stmt, 0);
      const int length = sqlite3_column_bytes(stmt, 0);
      res.set_content(blob, length, "application/vnd.mapbox-vector-tile");
    });
    // small chance that we could repeat tile build, but don't want to keep mutex locked during DB query
    if(res.body.empty()) {
      std::shared_future<std::string> fut;
      {
        std::lock_guard<std::mutex> lock(buildMutex);
        // check for pending build
        auto it = buildQueue.find(id);
        if(it != buildQueue.end()) {
          fut = it->second;
        }
        else {
          fut = std::shared_future<std::string>(buildWorkers.enqueue([&, id](){
            std::string mvt = buildTile(worldGOL, id);
            worldDB.putTile.bind(id.z, id.x, (1 << id.z) - 1 - id.y);
            sqlite3_bind_blob(worldDB.putTile.stmt, 4, mvt.data(), mvt.size(), SQLITE_STATIC);
            if(!worldDB.putTile.exec())
              LOG("Error adding tile %s to DB: %s", id.toString().c_str(), worldDB.errMsg());
            {
              std::lock_guard<std::mutex> lock(buildMutex);
              buildQueue.erase(id);
            }
            return mvt;
          }));
          buildQueue.emplace(id, fut);
        }
      }
      if(fut.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
        return httplib::StatusCode::RequestTimeout_408;  // 504 would be more correct
      }
      std::string mvt = fut.get();
      if(mvt.empty()) { return httplib::StatusCode::NotFound_404; }
      res.set_content(mvt.data(), mvt.size(), "application/vnd.mapbox-vector-tile");
    }

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
