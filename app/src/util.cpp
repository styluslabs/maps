#include "util.h"
#include "tangram.h"
#include "scene/scene.h"
#include "sqlite3/sqlite3.h"

using namespace Tangram;

template<typename T>
static constexpr T clamp(T val, T min, T max) {
    return val > max ? max : val < min ? min : val;
}

// https://stackoverflow.com/questions/27928
double lngLatDist(LngLat r1, LngLat r2)
{
  constexpr double p = 3.14159265358979323846/180;
  double a = 0.5 - cos((r2.latitude-r1.latitude)*p)/2 + cos(r1.latitude*p) * cos(r2.latitude*p) * (1-cos((r2.longitude-r1.longitude)*p))/2;
  return 12742 * asin(sqrt(a));  // kilometers
}

TileID lngLatTile(LngLat ll, int z)
{
  int x = int(floor((ll.longitude + 180.0) / 360.0 * (1 << z)));
  double latrad = ll.latitude * M_PI/180.0;
  int y = int(floor((1.0 - asinh(tan(latrad)) / M_PI) / 2.0 * (1 << z)));
  return TileID(x, y, z);
}

// segfault if GLM_FORCE_CTOR_INIT is defined for some units and not others!!!
LngLat tileCoordToLngLat(const TileID& tileId, const glm::vec2& tileCoord)
{
  double scale = MapProjection::metersPerTileAtZoom(tileId.z);
  ProjectedMeters tileOrigin = MapProjection::tileSouthWestCorner(tileId);
  ProjectedMeters meters = glm::dvec2(tileCoord) * scale + tileOrigin;
  return MapProjection::projectedMetersToLngLat(meters);
}

std::string yamlToStr(const YAML::Node& node)
{
  YAML::Emitter emitter;
  emitter.SetSeqFormat(YAML::Flow);
  emitter.SetMapFormat(YAML::Flow);
  emitter << node;
  return std::string(emitter.c_str());
}

// note that indices for sqlite3_column_* start from 0 while indices for sqlite3_bind_* start from 1
bool DB_exec(sqlite3* db, const char* sql, SQLiteStmtFn cb, SQLiteStmtFn bind)
{
  //if(sqlite3_exec(searchDB, sql, cb ? sqlite_static_helper : NULL, cb ? &cb : NULL, &zErrMsg) != SQLITE_OK) {
  auto t0 = std::chrono::high_resolution_clock::now();
  int res;
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
    logMsg("sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(db));
    return false;
  }
  if(bind)
    bind(stmt);
  while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {
    if(cb)
      cb(stmt);
  }
  if(res != SQLITE_DONE && res != SQLITE_OK)
    logMsg("sqlite3_step error: %s\n", sqlite3_errmsg(db));
  sqlite3_finalize(stmt);
  auto t1 = std::chrono::high_resolution_clock::now();
  //logMsg("Query time: %.6f s for %s\n", std::chrono::duration<float>(t1 - t0).count(), sql);
  return true;
}

/*

// common fns

template<typename ... Args>
std::string fstring(const char* format, Args ... args)
{
    int size_s = std::snprintf( nullptr, 0, format, args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ) return "";
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format, args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}

template<typename T>
std::string joinStr(const std::vector<T>& strs, const char* sep)
{
  std::stringstream ss;
  if(!strs.empty())
    ss << strs[0];
  for(size_t ii = 1; ii < strs.size(); ++ii)
    ss << sep << strs[ii];
  return ss.str();
}

*/
