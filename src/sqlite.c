/* ********************************************************** */
/* -*- sqlite.c -*- Helper routines for sqlite            -*- */
/* ********************************************************** */
/* Tyler Besselman (C) December 2024                          */
/* ********************************************************** */

#include <sqlite.h>
#include <stdio.h>

sqlite3 *sqlite_open(const char *path, int readonly)
{
    int flags = (readonly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    sqlite3 *db;

    int res = sqlite3_open_v2(path, &db, flags, NULL);

    if (res != SQLITE_OK) {
        _sqlerror("sqlite3_open", res);
        return NULL;
    } else {
        return db;
    }
}

// Callback for sqlite3_exec, wrapping a block execution.
static int _sqlite_exec_callback(void *block, int cols, char **cvals, char **cnames)
{
    if (!block) { return 0; }

    int (^callback)(int, char **, char **) = (int (^)(int, char **, char **))block;
    return callback(cols, cvals, cnames);
}

int sqlite_exec(sqlite3 *db, const char *query, int (^callback)(int cols, char **cvals, char **cnames))
{
    if (DEBUG_SQLITE) {
        printf("sqlite_exec: '%s'\n", query);
    }

    // Possible error message.
    char *err;

    // Execute the query.
    int res = sqlite3_exec(db, query, _sqlite_exec_callback, callback, &err);

    if (res != SQLITE_OK && res != SQLITE_ABORT)
    {
        if (err) {
            fprintf(stderr, "sqlite3_exec: %s\n", err);
            sqlite3_free(err);
        } else {
            sqlerror("sqlite3_exec", db);
        }

        return 1;
    }

    return 0;
}

sqlite3_stmt *sqlite_prepare(sqlite3 *db, const char *query)
{
    sqlite3_stmt *res;

    if (DEBUG_SQLITE) {
        printf("sqlite_prepare: '%s'\n", query);
    }

    int code = sqlite3_prepare_v2(db, query, -1, &res, NULL);

    if (code != SQLITE_OK)
    {
        sqlerror("sqlite3_prepare", db);
        return NULL;
    }

    return res;
}

int sqlite_bind_str(sqlite3_stmt *statement, int loc, const char *str)
{
    int code = sqlite3_bind_text(statement, loc, str, -1, SQLITE_STATIC);

    if (code != SQLITE_OK) { _sqlerror("sqlite3_bind", code); }
    return (code != SQLITE_OK);
}

int sqlite_bind_int(sqlite3_stmt *statement, int loc, int val)
{
    int code = sqlite3_bind_int(statement, loc, val);

    if (code != SQLITE_OK) { _sqlerror("sqlite3_bind", code); }
    return (code != SQLITE_OK);
}

int sqlite_bind_null(sqlite3_stmt *statement, int loc)
{
    int code = sqlite3_bind_null(statement, loc);

    if (code != SQLITE_OK) { _sqlerror("sqlite3_bind", code); }
    return (code != SQLITE_OK);
}

int sqlite_step(sqlite3_stmt *statement)
{
    int code = sqlite3_step(statement);

    if (code != SQLITE_ROW && code != SQLITE_DONE) {
        _sqlerror("sqlite3_bind", code);
    }

    return code;
}

int sqlite_close(sqlite3 *db)
{
    int code = sqlite3_close_v2(db);

    if (code != SQLITE_OK) {
        _sqlerror("sqlite3_close", code);
        return 1;
    } else {
        return 0;
    }
}
