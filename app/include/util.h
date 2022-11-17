#pragma once

//#include "ulib/stringutil.h"
//#include "ulib/threadutil.h"

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

#include <sstream>

template<template<class, class...> class Container, class... Container_Params>
Container<std::string, Container_Params... > splitStr(std::string s, const char* delims, bool skip_empty = false)
{
  Container<std::string, Container_Params... > elems;
  std::string item;
  size_t start = 0, end = 0;
  while((end = s.find_first_of(delims, start)) != std::string::npos) {
    if(!skip_empty || end > start)
      elems.insert(elems.end(), s.substr(start, end-start));
    start = end + 1;
  }
  if(start < s.size())
    elems.insert(elems.end(), s.substr(start));
  return elems;
}

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

#include <mutex>
#include <condition_variable>
#include <climits>

class Semaphore
{
private:
  std::mutex mtx;
  std::condition_variable cond;
  unsigned long cnt = 0;
  const unsigned long max;

public:
  Semaphore(unsigned long _max = ULONG_MAX) : max(_max) {}

  void post()
  {
    std::lock_guard<decltype(mtx)> lock(mtx);
    cnt = std::min(max, cnt+1);
    cond.notify_one();
  }

  void wait()
  {
    std::unique_lock<decltype(mtx)> lock(mtx);
    cond.wait(lock, [this](){ return cnt > 0; });
    --cnt;
  }

  // returns true if semaphore was signaled, false if timeout occurred
  bool waitForMsec(unsigned long ms)
  {
    std::unique_lock<decltype(mtx)> lock(mtx);
    bool res = cond.wait_for(lock, std::chrono::milliseconds(ms), [this](){ return cnt > 0; });
    if(res)
      --cnt;
    return res;
  }
};
