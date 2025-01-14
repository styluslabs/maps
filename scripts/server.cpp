//#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "sqlitepp.h"
#include "ulib/threadutil.h"

#include <map>

#define LOG(...) fprintf(stderr, __VA_ARGS__)

int main(int argc, char* argv[])
{
  static const int BLKZ = 10;
  static const char* getTileSQL = 
      "SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;";

  SQLiteDB worldDB;
  std::map<std::string, SQLiteDB> openDBs;
  ThreadPool buildWorkers(1);  // ThreadPool(1) is like AsyncWorker
  httplib::Server svr;
  //httplib::SSLServer svr;

  svr.Get("/tiles/:z/:x/:y", [&](const httplib::Request& req, httplib::Response& res) {
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
      auto blkid = std::to_string(BLKZ) + "/" + std::to_string(blkx) + "/" + std::to_string(blky);
      auto blkit = openDBs.find(blkid);
      if(blkit == openDBs.end()) {
        SQLiteDB& db = openDBs[blkid];

        std::string dbfile = blkid + ".mbtiles";

        auto dbmode = SQLITE_OPEN_READONLY;
        if(sqlite3_open_v2(dbfile.c_str(), &db.db, dbmode, NULL) != SQLITE_OK) {
          auto buildfn = [&db, blkid]() {
            std::string outfile = blkid + ".mbtiles";
            const char* cmd = "tilemaker --bbox %.9f %.9f %.9f %.9f --input planet.osm.pbf --output %s";
            FILE* pipe = popen(cmd, "r");
            if(!pipe) { throw std::runtime_error("popen() failed!"); }
            while(fgets(buffer.data(), int(buffer.size()), pipe) != nullptr) { result += buffer.data(); }
            int res = pclose(pipe);
            if(res != 0) {
              LOG("tilemaker failed with error code %d", res);
            } else {
              auto dbmode = SQLITE_OPEN_READONLY;
              if(sqlite3_open_v2(outfile.c_str(), &db.db, dbmode, NULL) != SQLITE_OK) {
                LOG("Error opening %s: %s", outfile.c_str(), db.errMsg());
              }
            }
          };
          buildWorkers.enqueue(buildfn);
          return httplib::StatusCode::NotFound_404;
        }        
      }
      if(!blkit->second.db) 
        return httplib::StatusCode::NotFound_404;
      // tile is available!
      blkit->second.stmt(getTileSQL).bind(z, x, ydb).exec([&](sqlite3_stmt* stmt){
        const char* blob = (const char*) sqlite3_column_blob(stmt, 0);
        const int length = sqlite3_column_bytes(stmt, 0);
        res.set_content(blob, length, "application/vnd.mapbox-vector-tile");
      });
    }

    res.set_header("Content-Encoding", "gzip");
    return httplib::StatusCode::OK_200;
  });

  svr.listen("0.0.0.0", 8080);
}