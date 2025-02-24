// Serve tiles from multiple mbtiles files, generating missing tiles on demand

#include <map>
#include "tilebuilder.h"
#define SQLITEPP_LOGE LOG
#define SQLITEPP_LOGW LOG
#include "sqlitepp.h"
#include "ulib/threadutil.h"

#define STRINGUTIL_IMPLEMENTATION
#include "ulib/stringutil.h"

// httplib should be last include because it pulls in, e.g., fcntl.h with #defines that break geodesk headers
//#define CPPHTTPLIB_OPENSSL_SUPPORT
//#define CPPHTTPLIB_ZLIB_SUPPORT
#include "httplib.h"

extern std::string buildTile(Features& world, TileID id);

static const char* getTileSQL =
    "SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;";
static const char* putTileSQL =
    "INSERT INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?,?,?,?);";

class TileDB : public SQLiteDB
{
public:
  SQLiteStmt getTile = {NULL};
  SQLiteStmt putTile = {NULL};
};

thread_local TileDB worldDB;

int main(int argc, char* argv[])
{
  struct Stats_t { std::atomic_uint_fast64_t reqs = 0, reqsok = 0, bytesout = 0, tilesbuilt = 0; } stats;
  const char* worldDBPath = argc > 2 ? argv[2] : "planet.mbtiles";
  //static const int BLKZ = 8;
  static const char* schemaSQL = R"(BEGIN;
    CREATE TABLE IF NOT EXISTS tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob);
    CREATE UNIQUE INDEX IF NOT EXISTS tile_index on tiles (zoom_level, tile_column, tile_row);
  COMMIT;)";

  if(argc < 2) {
    LOG("No gol file specified!");
    return -1;
  }
  Features worldGOL(argv[1]);
  LOG("Loaded %s", argv[1]);

  //std::map<std::string, TileDB> openDBs;
  // ... separate queues for high zoom and low zoom (slower build)?
  std::mutex buildMutex;
  std::map< TileID, std::shared_future<std::string> > buildQueue;
  ThreadPool buildWorkers(1);  // ThreadPool(1) is like AsyncWorker
  httplib::Server svr;  //httplib::SSLServer svr;
  auto time0 = std::chrono::steady_clock::now();

  sqlite3_config(SQLITE_CONFIG_MULTITHREAD);  // should be OK since our DB object is declared thread_local
  if(worldDB.open(worldDBPath, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) != SQLITE_OK) {  //SQLITE_OPEN_READONLY
    LOG("Error opening world mbtiles %s\n", worldDBPath);
    return -1;
  }
  worldDB.exec(schemaSQL);

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

    if(!worldDB.db) {
      if(worldDB.open(worldDBPath, SQLITE_OPEN_READONLY) != SQLITE_OK) {
        LOG("Error opening DB on http worker thread!");
        return httplib::StatusCode::InternalServerError_500;
      }
      worldDB.getTile = worldDB.stmt(getTileSQL);
    }

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
            if(!worldDB.db) {
              if(worldDB.open(worldDBPath, SQLITE_OPEN_READWRITE) != SQLITE_OK) {
                LOG("Error opening DB on tile builder thread!");
                return std::string();
              }
              worldDB.putTile = worldDB.stmt(putTileSQL);
            }
            std::string mvt = buildTile(worldGOL, id);
            if(!mvt.empty()) {
              worldDB.putTile.bind(id.z, id.x, (1 << id.z) - 1 - id.y);
              sqlite3_bind_blob(worldDB.putTile.stmt, 4, mvt.data(), mvt.size(), SQLITE_STATIC);
              if(!worldDB.putTile.exec())
                LOG("Error adding tile %s to DB: %s", id.toString().c_str(), worldDB.errMsg());
            }
            { std::lock_guard<std::mutex> lock(buildMutex);  buildQueue.erase(id); }
            return mvt;
          }));
          buildQueue.emplace(id, fut);
          ++stats.tilesbuilt;
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
