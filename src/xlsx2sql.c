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
#define SQL_CREATE_HDR  "create table ?1 (id integer primary key"
#define SQL_CREATE_TAIL ") strict;"

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

// Build create table query for validated xlsx doc.
static char *build_create_query(struct xlsx *doc, int *types)
{
    // In the create statement each column becomes a string "?NN type, " where type is "integer" or "text.
    // "integer" has 7 chars. There are 4 chars that are added unconditionally.
    // The number NN has at most the number of digits in the column count.
    size_t append_max = 11 + digits(xlsx_cols(doc));

    size_t bsize = strlen(SQL_CREATE_HDR) + (xlsx_cols(doc) * append_max) + strlen(SQL_CREATE_TAIL) + 1;
    char *query = malloc(bsize);

    if (!query)
    {
        perror("malloc");
        return NULL;
    }

    off_t i = strlcpy(query, SQL_CREATE_HDR, bsize);
    bsize -= i;

    for (size_t col = 0; col < xlsx_cols(doc); col++)
    {
        if (types[col] == XLSX_TYPE_NULL)
        {
            fprintf(stderr, "Warning: Skipping empty column %zu\n", col + 1);
            continue;
        }

        const char *type = (types[col] == XLSX_TYPE_INT ? "integer" : "text");
        int cnt = snprintf(&query[i], bsize, ", ?%zu %s", col + 2, type);

        if (cnt < 0)
        {
            perror("snprintf");
            free(query);

            return NULL;
        }

        i += cnt;
        bsize -= cnt;
    }

    size_t cnt = strlcpy(&query[i], SQL_CREATE_TAIL, bsize);

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
    char *query = build_create_query(doc, types);
    if (!query) { return 1; }

    printf("Built create query: '%s'\n", query);
    printf("Creating table '%s'...\n", name);

    sqlite3_stmt *stmt = sqlite_prepare(db, query);
    if (!stmt) { return 1; }

    if (sqlite_bind_str(stmt, 1, name))
    {
        sqlite3_finalize(stmt);
        return 1;
    }

    struct xlsx_value *header = xlsx_row(doc, 0);

    for (size_t col = 0; col < xlsx_cols(doc); col++)
    {
        char *cname = header[col].type == XLSX_TYPE_STR ? xlsx_str(doc, &header[col]) : header[col].str;
        printf("Bind column '%s'\n", cname);

        if (sqlite_bind_str(stmt, col + 2, cname))
        {
            sqlite3_finalize(stmt);
            return 1;
        }
    }

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        fprintf(stderr, "Error: Failed to create table\n");
        sqlite3_finalize(stmt);

        return 1;
    }

    sqlite3_finalize(stmt);
    return 0;
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
            return 1;
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

            return 1;
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
        xlsx_doc_free(doc);

        return 1;
    }

    // We create a type map during validation so we can set table column types properly.
    int *types = calloc(xlsx_cols(doc), sizeof(int));

    if (!types)
    {
        xlsx_doc_free(doc);
        perror("calloc");

        return 1;
    }

    if (check_document(doc, types))
    {
        xlsx_doc_free(doc);
        free(types);

        return 1;
    }

    sqlite3 *db = sqlite_open(db_path, false);
    char *tblname = filename(db_path);

    if (!db || !tblname)
    {
        xlsx_doc_free(doc);
        free(types);

        if (tblname) {
            free(tblname);
        }

        return 1;
    }

    if (create_table(db, tblname, doc, types))
    {
        xlsx_doc_free(doc);
        free(tblname);
        free(types);

        return 1;
    }

    printf("Successfully created table '%s'\n", tblname);
    free(tblname);

    free(types);
    xlsx_doc_free(doc);

    return 0;
}
