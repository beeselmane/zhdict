#ifndef __SQLITE__
#define __SQLITE__ 1

#include <sqlite3.h>
#include <stdio.h>

// Database error printing (raw error code)
static inline void _sqlerror(const char *func, int code)
{ fprintf(stderr, "%s: %s (%d)\n", func, sqlite3_errstr(code), code); }

// Database error printing
static inline void sqlerror(const char *func, sqlite3 *db)
{ fprintf(stderr, "%s: %s (%d)\n", func, sqlite3_errmsg(db), sqlite3_errcode(db)); }

// Open a database at path, printing an error and returning NULL on failure.
extern sqlite3 *sqlite_open(const char *path, int readonly);

// Execute a query, passing the result to a callback.
extern int sqlite_exec(sqlite3 *db, const char *query, int (^callback)(int cols, char **cvals, char **cnames));

// Prepare a statement from a given query.
extern sqlite3_stmt *sqlite_prepare(sqlite3 *db, const char *query);

// Bind a string
extern int sqlite_bind_str(sqlite3_stmt *statement, int loc, const char *str);

// Bind a number
extern int sqlite_bind_int(sqlite3_stmt *statement, int loc, int val);

// Execute a statement
extern int sqlite_step(sqlite3_stmt *statement);

extern int sqlite_col_str(sqlite3_stmt *statement, int col);
extern int sqlite_col_int(sqlite3_stmt *statement, int col);

// Close a database connection.
extern int sqlite_close(sqlite3 *db);

#endif /* !defined(__SQLITE__) */
