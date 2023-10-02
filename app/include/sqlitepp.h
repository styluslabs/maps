#pragma once
#include "sqlite3/sqlite3.h"
#include "log.h"
#include <tuple>
#include <functional>

template<class F, class Tuple, size_t... Is>
constexpr auto apply_impl(Tuple t, F f, std::index_sequence<Is...>)
{
  return f(std::get<Is>(t)...);
}

template<class F, class Tuple>
constexpr auto apply_tuple(F f, Tuple t)
{
  return apply_impl(t, f, std::make_index_sequence<std::tuple_size<Tuple> {}> {});
}

// from https://github.com/SqliteModernCpp/sqlite_modern_cpp - extract signature from all possible fn types
template<typename> struct func_traits;

template <typename F>
struct func_traits : public func_traits< decltype(&std::remove_reference<F>::type::operator()) > {};

template <typename Cls, typename Ret, typename... Args>
struct func_traits<Ret(Cls::*)(Args...) const> : func_traits<Ret(*)(Args...)> {};

template <typename Cls, typename Ret, typename... Args>
struct func_traits<Ret(Cls::*)(Args...)> : func_traits<Ret(*)(Args...)> {};  // non-const

template <typename Ret, typename... Args>
struct func_traits<Ret(*)(Args...)> {
  typedef Ret result_type;
  using argument_tuple = std::tuple<Args...>;
  template <std::size_t Index>
  using argument = typename std::tuple_element<Index,	argument_tuple>::type;
  static const std::size_t arity = sizeof...(Args);
};

// of course there are many sqlite C++ wrappers and ORMs, but many (most?) don't even provide variadic bind()
//  and only https://github.com/SqliteModernCpp/sqlite_modern_cpp seems to support extracting column types
//  from callback fn signature
class SQLiteStmt
{
public:
  // static (thread_local) SQLiteStmt is actually pretty reasonable for DBs open for the entire life of the
  //  application; another approach is SQLiteDB.stmt() returning and caching ref to SQLiteStmt
#ifndef SQLITEPP_STMT_NO_STATIC
  static bool dbClosed;
#endif

  sqlite3_stmt* stmt = NULL;
  SQLiteStmt(sqlite3_stmt* _stmt) : stmt(_stmt) {}
  SQLiteStmt(const SQLiteStmt&) = delete;
  ~SQLiteStmt() {
#ifndef SQLITEPP_STMT_NO_STATIC
    if(dbClosed) return;
#endif
    sqlite3_finalize(stmt);
  }

  SQLiteStmt(sqlite3* db, const char* sql) {
    if(sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK)
      LOGE("sqlite3_prepare_v2 error: %s", sqlite3_errmsg(db));
  }

  void bind_at(int loc) {}
  template<class... Args> void bind_at(int loc, bool arg0, Args... args)
  { sqlite3_bind_int(stmt, loc, arg0 ? 1 : 0); bind_at(loc+1, args...); }
  template<class... Args> void bind_at(int loc, int arg0, Args... args)
  { sqlite3_bind_int(stmt, loc, arg0); bind_at(loc+1, args...); }
  template<class... Args> void bind_at(int loc, sqlite_int64 arg0, Args... args)
  { sqlite3_bind_int64(stmt, loc, arg0); bind_at(loc+1, args...); }
  template<class... Args> void bind_at(int loc, double arg0, Args... args)
  { sqlite3_bind_double(stmt, loc, arg0); bind_at(loc+1, args...); }
  template<class... Args> void bind_at(int loc, const char* arg0, Args... args)
  { sqlite3_bind_text(stmt, loc, arg0, -1, SQLITE_TRANSIENT); bind_at(loc+1, args...); }  //SQLITE_STATIC
  template<class... Args> void bind_at(int loc, const std::string& arg0, Args... args)
  { sqlite3_bind_text(stmt, loc, arg0.c_str(), int(arg0.size()), SQLITE_TRANSIENT); bind_at(loc+1, args...); }

  template<class... Args> SQLiteStmt& bind(Args... args) { if(stmt) bind_at(1, args...); return *this; }

  template<class T> T get_col(int idx);

  template<class T> std::tuple<T> columns(int idx) { return std::make_tuple(get_col<T>(idx)); }

  template<class T1, class T2, class... Args>
  std::tuple<T1, T2, Args...> columns(int idx) {
    return std::tuple_cat(columns<T1>(idx), columns<T2, Args...>(idx+1));  //sizeof...(Args)
  }

  // have to use struct to extract parameter pack
  template <typename> struct _columns;
  template <typename... Args>
  struct _columns< std::tuple<Args...> > {
    static std::tuple<Args...> get(SQLiteStmt& inst) { return inst.columns<Args...>(0); }
  };

  template<class F>
  bool exec(F&& cb) {
    if(!stmt) return false;
    //auto t0 = std::chrono::high_resolution_clock::now();
    int res;
    while((res = sqlite3_step(stmt)) == SQLITE_ROW) {
      //if(cb)
        apply_tuple(cb, _columns<typename func_traits<F>::argument_tuple>::get(*this));
    }
    bool ok = res == SQLITE_DONE || res == SQLITE_OK;
    if(!ok)
      LOGE("sqlite3_step error: %s", sqlite3_errmsg(sqlite3_db_handle(stmt)));
    sqlite3_reset(stmt);
    return ok;
  }

  bool exec() { return exec([](sqlite3_stmt*){}); }
};

// if we want to use static SQLiteStmt instances, we can set a static flag SQLiteStmt::dbClosed when main()
//  exits to tell destructor to skip sqlite3_finalize()

// can't be inside class due to GCC bug
template<> inline int SQLiteStmt::get_col(int idx) { return sqlite3_column_int(stmt, idx); }
template<> inline sqlite_int64 SQLiteStmt::get_col(int idx) { return sqlite3_column_int64(stmt, idx); }
template<> inline float SQLiteStmt::get_col(int idx) { return float(sqlite3_column_double(stmt, idx)); }
template<> inline double SQLiteStmt::get_col(int idx) { return sqlite3_column_double(stmt, idx); }
template<> inline const char* SQLiteStmt::get_col(int idx) { return (const char*)sqlite3_column_text(stmt, idx); }
template<> inline std::string SQLiteStmt::get_col(int idx) { return (const char*)sqlite3_column_text(stmt, idx); }
template<> inline sqlite3_stmt* SQLiteStmt::get_col(int idx) { return stmt; }
