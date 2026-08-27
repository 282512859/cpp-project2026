// 负责人：成员2：服务端存储
#pragma once

// A deliberately small declaration shim. The application still links to the
// official SQLite library; this keeps the repository independent of where a
// package manager places sqlite3.h on Windows.
extern "C" {
struct sqlite3;
struct sqlite3_stmt;
using sqlite3_int64 = long long;
using sqlite3_destructor_type = void (*)(void*);

int sqlite3_open(const char*, sqlite3**);
int sqlite3_close(sqlite3*);
const char* sqlite3_errmsg(sqlite3*);
int sqlite3_exec(sqlite3*, const char*, int (*)(void*,int,char**,char**), void*, char**);
void sqlite3_free(void*);
int sqlite3_prepare_v2(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
int sqlite3_step(sqlite3_stmt*);
int sqlite3_finalize(sqlite3_stmt*);
int sqlite3_reset(sqlite3_stmt*);
int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int, sqlite3_destructor_type);
int sqlite3_bind_int64(sqlite3_stmt*, int, sqlite3_int64);
int sqlite3_bind_null(sqlite3_stmt*, int);
sqlite3_int64 sqlite3_column_int64(sqlite3_stmt*, int);
int sqlite3_column_int(sqlite3_stmt*, int);
const unsigned char* sqlite3_column_text(sqlite3_stmt*, int);
int sqlite3_column_type(sqlite3_stmt*, int);
sqlite3_int64 sqlite3_last_insert_rowid(sqlite3*);
int sqlite3_changes(sqlite3*);
}

inline constexpr int SQLITE_OK = 0;
inline constexpr int SQLITE_ROW = 100;
inline constexpr int SQLITE_DONE = 101;
inline constexpr int SQLITE_NULL = 5;
#define SQLITE_TRANSIENT ((sqlite3_destructor_type)-1)
