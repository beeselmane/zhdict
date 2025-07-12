/* ********************************************************** */
/* -*- xlsx2sql.c -*- Convert XLSX document to sqlite     -*- */
/* ********************************************************** */
/* Tyler Besselman (C) June 2025                              */
/* ********************************************************** */

// Convert an XLSX document with column headings to a sqlite database with a single table
//   and column names taken from the first row of the XLSX document.

#include <strings.h>
#include <stdbool.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>

#include <sqlite.h>
#include <xlsx.h>

// The create query starts and ends with these strings.
// The center of the query is filled in dynamically based on the columns & types of the xlsx doc.
#define SQL_CREATE_HDR_1 "create table "
#define SQL_CREATE_HDR_2 " (id integer primary key"
#define SQL_CREATE_TAIL  ") strict;"

// Similar to the create query but for the insert query
#define SQL_INSERT_HDR_1 "insert into "
#define SQL_INSERT_HDR_2 " values(?1"
#define SQL_INSERT_TAIL  ") returning id;"

// Given we know entry->type is STR or LSTR get the string value of entry
#define XLSX_STRVAL(entry) (((entry)->type == XLSX_TYPE_STR) ? xlsx_str(doc, (entry)) : (entry)->str)

// Count # of base 10 digits in the number n
static inline int digits(size_t n)
{
    if (!n) { return 1; }
    int i = 0;

    while (n)
    {
        n /= 10;
        i++;
    }

    return i;
}

// Get the name of the file at a path without the extension
static inline char *filename(char *path)
{
    char *base = basename(path);

    if (!base)
    {
        perror("basename");
        return NULL;
    }

    // Find extension if exists.
    char *ext = strrchr(base, '.');

    size_t len = (ext ? (uintptr_t)(ext - base) : strlen(base));
    char *name = malloc(len + 1);

    memcpy(name, base, len);
    name[len] = '\0';

    return name;
}

// Create insertion statement for a given doc.
static char *build_insert_query(const char *name, struct xlsx *doc)
{
    // insert into `name` values(?1, ?2, ..., ?n) returning id;
    // Here, ?1 is the id and ?2...?n are the xlsx columns
    // Thus, there are 3 chars added unconditionally and a number
    //   which is maxially the number of digits in the column count.
    size_t append_max = digits(xlsx_cols(doc) + 1) + 3;

    size_t base_len = strlen(SQL_INSERT_HDR_1 SQL_INSERT_HDR_2 SQL_INSERT_TAIL);
    size_t bsize = base_len + (xlsx_cols(doc) * append_max) + 1;
    char *query = malloc(bsize);

    if (!query)
    {
        perror("malloc");
        return NULL;
    }

    off_t i = strlcpy(query, SQL_INSERT_HDR_1, bsize);
    bsize -= i;

    size_t cnt = strlcpy(&query[i], name, bsize);
    bsize -= cnt;
    i += cnt;

    cnt = strlcpy(&query[i], SQL_INSERT_HDR_2, bsize);
    bsize -= cnt;
    i += cnt;

    for (size_t col = 0; col < xlsx_cols(doc); col++)
    {
        int cnt = snprintf(&query[i], bsize, ", ?%zu", col + 2);

        if (cnt < 0)
        {
            perror("snprintf");
            free(query);

            return NULL;
        }

        i += cnt;
        bsize -= cnt;
    }

    cnt = strlcpy(&query[i], SQL_INSERT_TAIL, bsize);

    if (cnt != strlen(SQL_INSERT_TAIL))
    {
        fprintf(stderr, "Error: Ran out of space when copying!\n");
        free(query);

        return NULL;
    }

    return query;
}

// Insert all rows into the table we made.
static int insert_rows(sqlite3 *db, const char *name, struct xlsx *doc, int *types)
{
    char *query = build_insert_query(name, doc);
    if (!query) { return 1; }

    printf("Built insert query: '%s'\n", query);

    sqlite3_stmt *stmt = sqlite_prepare(db, query);

    if (!stmt)
    {
        free(query);
        return 1;
    }

    printf("Inserting %zu rows...\n", xlsx_rows(doc) - 1);
    size_t mod = xlsx_rows(doc) < 10000 ? 10 : xlsx_rows(doc) / 100;

    int result = xlsx_foreach_row(doc, ^(struct xlsx_value *entry, size_t i) {
        if (!i) { return 0; }

        if (!(i % mod)) {
            printf("Insert %zu...", i);
        }

        // Checked bind macro.
        #define CHECK(s)                    \
            do {                            \
                if (s)                      \
                {                           \
                    if (!(i % mod)) {       \
                        printf(" [err]\n"); \
                    }                       \
                                            \
                    sqlerror("bind", db);   \
                    return 1;               \
                }                           \
            } while (0)

        CHECK(sqlite_bind_int(stmt, 1, i));

        for (size_t col = 0; col < xlsx_cols(doc); col++)
        {
            if (entry[col].type == XLSX_TYPE_INT) {
                CHECK(sqlite_bind_int(stmt, col + 2, entry[col].ival));
            } else if (entry[col].type == XLSX_TYPE_NULL) {
                CHECK(sqlite_bind_null(stmt, col + 2));
            } else {
                //printf("Bind %s = %s", XLSX_STRVAL(&header[col]), XLSX_STRVAL(&entry[col]));
                CHECK(sqlite_bind_str(stmt, col + 2, XLSX_STRVAL(&entry[col])));
            }
        }

        #undef CHECK

        int status = sqlite3_step(stmt);

        if (status == SQLITE_ROW) {
            if (!(i % mod)) {
                printf(" [%u]\n", sqlite3_column_int(stmt, 0));
            }

            sqlite3_reset(stmt);
            return 0;
        } else {
            if (!(i % mod)) {
                printf(" [err]\n");
            }

            sqlerror("sqlite3_step", db);
            return 1;
        }
    });

    sqlite3_finalize(stmt);
    free(query);

    return result;
}

// Build create table query for validated xlsx doc.
// We take strings from the header directly for column names,
//   so it's possible to make bad things happen if column names are bad.
static char *build_create_query(const char *name, struct xlsx *doc, int *types)
{
    // We need to know the max column name length to get a big enough buffer.
    struct xlsx_value *header = xlsx_row(doc, 0);
    size_t append_max = 0;

    for (size_t col = 0; col < xlsx_cols(doc); col++)
    {
        // We know the type is either a string or a literal string here.
        size_t len = strlen(XLSX_STRVAL(&header[col]));

        if (len > append_max) {
            append_max = len;
        }
    }

    // In the create statement each column becomes a string "col type, " where type is "integer" or "text.
    // "integer" has 7 chars. There are 4 chars that are added unconditionally.
    append_max += 11;

    size_t base_len = strlen(SQL_CREATE_HDR_1 SQL_CREATE_HDR_2 SQL_CREATE_TAIL);
    size_t bsize = base_len + (xlsx_cols(doc) * append_max) + 1;
    char *query = malloc(bsize);

    if (!query)
    {
        perror("malloc");
        return NULL;
    }

    off_t i = strlcpy(query, SQL_CREATE_HDR_1, bsize);
    bsize -= i;

    size_t cnt = strlcpy(&query[i], name, bsize);
    bsize -= cnt;
    i += cnt;

    cnt = strlcpy(&query[i], SQL_CREATE_HDR_2, bsize);
    bsize -= cnt;
    i += cnt;

    for (size_t col = 0; col < xlsx_cols(doc); col++)
    {
        if (types[col] == XLSX_TYPE_NULL)
        {
            fprintf(stderr, "Warning: Skipping empty column %zu\n", col + 1);
            continue;
        }

        const char *type = (types[col] == XLSX_TYPE_INT ? "integer" : "text");
        const char *name = XLSX_STRVAL(&header[col]);

        // We truncate `name` at the first space if it exists.
        char *space = strchr(name, ' ');

        int len = (space ? space - name : strlen(name));
        int cnt = snprintf(&query[i], bsize, ", %.*s %s", len, name, type);

        if (cnt < 0)
        {
            perror("snprintf");
            free(query);

            return NULL;
        }

        i += cnt;
        bsize -= cnt;
    }

    cnt = strlcpy(&query[i], SQL_CREATE_TAIL, bsize);

    if (cnt != strlen(SQL_CREATE_TAIL))
    {
        fprintf(stderr, "Error: Ran out of space when copying!\n");
        free(query);

        return NULL;
    }

    return query;
}

// Create table in database.
static int create_table(sqlite3 *db, const char *name, struct xlsx *doc, int *types)
{
    char *query = build_create_query(name, doc, types);
    if (!query) { return 1; }

    printf("Built create query: '%s'\n", query);
    printf("Creating table '%s'...\n", name);

    int status = sqlite_exec(db, query, NULL) != SQLITE_OK;
    free(query);

    return status;
}

// Check the provided document is something we can convert.
// Currently this means all columns have a clear data type.
// In the process, fill in types[col] for each column with the type of the column.
static int check_document(struct xlsx *doc, int *types)
{
    if (xlsx_rows(doc) < 2)
    {
        fprintf(stderr, "Error: No data in document.\n");
        return 1;
    }

    // We'll check the header contains strings.
    struct xlsx_value *header = xlsx_row(doc, 0);

    for (size_t col = 0; col < xlsx_cols(doc); col++)
    {
        if (header[col].type != XLSX_TYPE_STR && header[col].type != XLSX_TYPE_LSTR)
        {
            fprintf(stderr, "Error: Column %zu has improper header\n", col + 1);
            return 1;
        }

        if (strchr(XLSX_STRVAL(&header[col]), ' ')) {
            fprintf(stderr, "Warning: Column %zu contains a space in the header\n", col + 1);
        }

        types[col] = xlsx_row(doc, 1)[col].type;

        int ok = !xlsx_iter_col(doc, col, ^(struct xlsx_value *entry, size_t i) {
            // Colum headers should be strings.
            if (!i) { return 0; }

            // Empty entries can be any type.
            if (entry->type == XLSX_TYPE_NULL) { return 0; }

            // Decide to make this column whichever type we see first.
            if (types[col] == XLSX_TYPE_NULL)
            {
                types[col] = entry->type;
                return 0;
            }

            // If any types mismatch, we can't really handle this.
            return (types[col] != entry->type);
        });

        // If any columns fail, fail overall.
        if (!ok)
        {
            fprintf(stderr, "Error: Column %zu has multiple typed entries (guessed %d)\n", col + 1, types[col]);
            return 1;
        }

        if (types[col] == XLSX_TYPE_FLOAT)
        {
            fprintf(stderr, "Error: Column %zu has floating type.\n", col + 1);
            return 1;
        }
    }

    return 0;
}

int main(int argc, char *const *argv)
{
    const char *xlsx_path = NULL;
    char *db_path = NULL;

    if (argc == 4) {
        if (strcmp(argv[1], "-f"))
        {
            fprintf(stderr, "Error: Invalid first argument '%s'\n", argv[1]);
            return 1;
        }

        xlsx_path = argv[2];
        db_path = argv[3];

        if (unlink(db_path) && errno != ENOENT)
        {
            perror("unlink");
            exit(1);
        }
    } else if (argc == 3) {
        xlsx_path = argv[1];
        db_path = argv[2];

        int status = access(db_path, F_OK);

        if (errno != ENOENT)
        {
            if (!status) {
                fprintf(stderr, "Error: File already exists at path '%s'\n", db_path);
            } else {
                perror("access");
            }

            exit(1);
        }
    } else {
        fprintf(stderr, "Usage: %s [-f] input.xlsx output.sqlite\n", argv[0]);
        return 1;
    }

    struct xlsx *doc = xlsx_doc_at(xlsx_path);
    if (!doc) { return 1; }

    if (!xlsx_rows(doc) || !xlsx_cols(doc))
    {
        fprintf(stderr, "Error: Attempt to convert empty document.\n");
        exit(1);
    }

    // We create a type map during validation so we can set table column types properly.
    int *types = calloc(xlsx_cols(doc), sizeof(int));

    if (!types)
    {
        perror("calloc");
        exit(1);
    }

    if (check_document(doc, types)) {
        exit(1);
    }

    char *tblname = filename(db_path);
    if (!tblname) { exit(1); }

    sqlite3 *db = sqlite_open(db_path, false);
    if (!db) { exit(1); }

    if (create_table(db, tblname, doc, types))
    {
        sqlite_close(db);
        exit(1);
    }

    printf("Successfully created table '%s'\n", tblname);

    if (insert_rows(db, tblname, doc, types))
    {
        sqlite_close(db);
        exit(1);
    }

    printf("Finished inserting all rows from document.\n");

    free(types);
    free(tblname);
    sqlite_close(db);
    xlsx_doc_free(doc);

    return 0;
}
