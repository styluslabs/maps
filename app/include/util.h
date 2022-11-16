#pragma once

#include "ulib/stringutil.h"
#include "ulib/threadutil.h"

#include "tangram.h"
#include "glm/vec2.hpp"
using namespace Tangram;
namespace YAML { class Node; }

double lngLatDist(LngLat r1, LngLat r2);
TileID lngLatTile(LngLat ll, int z);
LngLat tileCoordToLngLat(const TileID& tileId, const glm::vec2& tileCoord);

std::string yamlToStr(const YAML::Node& node);

struct sqlite3_stmt;
struct sqlite3;
typedef std::function<void(sqlite3_stmt*)> SQLiteStmtFn;
bool DB_exec(sqlite3* db, const char* sql, SQLiteStmtFn cb = SQLiteStmtFn(), SQLiteStmtFn bind = SQLiteStmtFn());
