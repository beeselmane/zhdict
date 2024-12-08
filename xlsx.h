#ifndef __XLSX__
#define __XLSX__ 1

#include <stdint.h>
#include <stdlib.h>

// For xmlNodePtr
#include "xml.h"

// Enable debug messages
#define DEBUG_XLSX 1

// Dataset row entry value
struct xlsx_value {
    enum {
        // Empty value
        XLSX_TYPE_NULL = -1,

        // String table index
        XLSX_TYPE_STR,

        // Integer value
        XLSX_TYPE_INT,

        // Floating point value
        XLSX_TYPE_FLOAT,

        // Literal string
        XLSX_TYPE_LSTR
    } type;

    union {
        char *str;
        size_t sref;
        int64_t ival;
        double fval;
    };
};

// I only care about the actual data, not any of the visual info.
struct xlsx {
    struct xlsx_strtab {
        // Pointer to a big chunk of string pointers (`count` of them)
        char **base;

        // How many strings are in this table
        size_t count;

        // Because of the way we read these, the actual memory of the strings
        //   has its lifecycle tied to this XML document.
        xmlDocPtr ref;
    } strtab;

    // # of rows and columns in this document.
    size_t rows;
    size_t cols;

    // Everything is just stored as a big grid.
    struct xlsx_value *grid;
};

#define xlsx_rows(doc) ((doc)->rows)
#define xlsx_cols(doc) ((doc)->cols)

// Read in excel document at a given path.
extern struct xlsx *xlsx_doc_at(const char *path);

// Perform a block on each row in an excel document. There are `xlsx_cols(doc)` entries in the passed array. `n` is the row number. Return `0` to stop.
extern void xlsx_foreach_row(struct xlsx *doc, int (^blk)(struct xlsx_value *row, size_t n));

// Iterate a block over a column in an excel document. The block will get called on each entry in the given column.
// There are `xlsx_rows(doc)` entries in each column. `n` is the row number of the entry in this column. Return `0` to stop.
extern void xlsx_iter_col(struct xlsx *doc, size_t col, int (^blk)(struct xlsx_value *entry, size_t n));

// Free memory for an excel document, destroying it.
extern void xlsx_doc_free(struct xlsx *doc);

#endif /* !defined(__XLSX__) */
