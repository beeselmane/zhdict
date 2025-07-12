/* ********************************************************** */
/* -*- conv.c -*- Convert dictionary from xlsx to sqlite  -*- */
/* ********************************************************** */
/* Tyler Besselman (C) December 2024                          */
/* ********************************************************** */

#include <strings.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <sqldecl.h>
#include <sqlite.h>
#include <xlsx.h>

// A table indexed by the first byte of a UTF-8 codepoint
//   determining the length of the encoded char.
static const uint8_t UTF8_TRAILING_COUNT[0x100] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5
};

// A structure holding a sqlite database and the various prepared statements we use.
struct sqlite_state {
    // The open database
    sqlite3 *db;

    // Database path (on open)
    char *path;

    // Statement for inserting a new radical
    sqlite3_stmt *rad_insert;

    // Statement for updating a radical
    sqlite3_stmt *rad_update;

    // Statement for finding a radical (by char)
    sqlite3_stmt *rad_find;

    // Statement for inserting a new character
    sqlite3_stmt *char_insert;

    // Statement for updating a character
    sqlite3_stmt *char_update;

    // Statement for finding a character (by char)
    sqlite3_stmt *char_find;

    // Statement for inserting a new dictionary entry
    sqlite3_stmt *dict_insert;
};

// Map used for insertion.
// Each entry is indexed by parameter # in the corresponding insert statement
//   and holds the index of the corresponding xlsx column to take data from.
struct insert_map {
    // Map used for inserting characters (char # == 1)
    off_t charmap[SQL_INS_CHAR_CNT + 1];

    // Map used for inserting dictionary entries (char # >= 1)
    off_t dictmap[SQL_INS_DICT_CNT + 1];
};

// Single character info used for inserting data into db
struct charinfo {
    char *str;
    char *rad; // As a string; we look up the id on insert
    uint64_t strokes;
    uint64_t strokes_ext;
    char *zhuyin;
    char *pinyin;
    char *pronoun_other;
    uint64_t pronoun_order;
};

// Single dictionary entry used for inserting data into db
struct dictinfo {
    uint64_t id;
    uint64_t chars;
    char *str;
    char *definition;

    // This buffer is used for character info reference in words.
    // At most, each entry is allowed up to 6 characters.
    uint32_t charinfo[6];
};

// Setup sqlite state for database at `path`.
static int sqlite_setup(struct sqlite_state *state, const char *path)
{
    #define CHECK(stmt) if (!(stmt)) { goto fail; }

    state->db = sqlite_open(path, false);
    if (!state->db) { return -1; }

    // Save this.
    state->path = (char *)path;

    printf("Creating sqlite tables...\n");

    if (sqlite_exec(state->db,  (
        // Create radical table
        SQL_STMT_CREATE_RAD

        // Create character table
        SQL_STMT_CREATE_CHAR

        // Create dictionary table
        SQL_STMT_CREATE_DICT

        // Create indicies
        SQL_STMT_CREATE_INDEX
    ), NULL)) { goto fail; }

    printf("Prepare insert radical statement...\n");

    CHECK(state->rad_insert = sqlite_prepare(state->db, SQL_STMT_INSERT_RAD));

    printf("Prepare insert character statement...\n");

    CHECK(state->char_insert = sqlite_prepare(state->db, SQL_STMT_INSERT_CHAR));

    printf("Prepare insert dictionary statement...\n");

    CHECK(state->dict_insert = sqlite_prepare(state->db, SQL_STMT_INSERT_DICT));

    printf("Prepare update radical statement...\n");

    CHECK(state->rad_update = sqlite_prepare(state->db, SQL_STMT_UPDATE_RAD));

    printf("Prepare update character statement...\n");

    CHECK(state->char_update = sqlite_prepare(state->db, SQL_STMT_UPDATE_CHAR));

    printf("Prepare find radical statement...\n");

    CHECK(state->rad_find = sqlite_prepare(state->db, "select "
        SQL_TABLE_RAD_FIELD_ID ", " SQL_TABLE_RAD_FIELD_STROKES
        " from " SQL_TABLE_RAD_NAME
        " where " SQL_TABLE_RAD_FIELD_CHAR " = ?"
    ";"));

    printf("Prepare find character statement...\n");

    CHECK(state->char_find = sqlite_prepare(state->db, "select "
        SQL_TABLE_CHAR_FIELD_ID ", " SQL_TABLE_CHAR_FIELD_STROKES
        " from " SQL_TABLE_CHAR_NAME
        " where " SQL_TABLE_CHAR_FIELD_CHAR " = ?"
    ";"));

    return 0;

fail:
    sqlite_close(state->db);

    if (unlink(path)) {
        perror("unlink");
    }

    return 1;
    #undef CHECK
}

// Destroy sqlite state. Remove file at original path if requested.
static void sqlite_destroy(struct sqlite_state *state, bool do_unlink)
{
    if (state->db) {
        sqlite_close(state->db);
    }

    if (state->path && do_unlink && unlink(state->path)) {
        perror("unlink");
    }
}

// Find a radical, returning (non-zero) row offset if found, zero if not found, and negative if an error occurred
//static int do_find_rad(sqlite3_stmt *stmt, const char *rad, int *strokes)
//{
//    //return find_str(stmt, rad, strokes);
//}
//
//// Update the radical at `index` to have `strokes` strokes.
//static int do_update_rad(sqlite3_stmt *stmt, int index, int strokes)
//{
//    if (sqlite_bind_int(stmt, SQL_UPD_RAD_STROKES, strokes)) { return 1; }
//    if (sqlite_bind_int(stmt, SQL_UPD_RAD_ID,      index  )) { return 1; }
//
//    int result = sqlite_step(stmt);
//    sqlite3_reset(stmt);
//
//    return (result != SQLITE_DONE);
//}
//
//// Insert new radical with info, returning inserted index on success, negative on failure.
//static int do_insert_rad(sqlite3_stmt *stmt, const char *rad, int strokes)
//{
//    printf("Insert new radical '%s'\n", rad);
//
//    if (sqlite_bind_str(stmt, SQL_INS_RAD_CHAR,    rad    )) { return -1; }
//    if (sqlite_bind_int(stmt, SQL_INS_RAD_STROKES, strokes)) { return -1; }
//
//    int status = sqlite3_step(stmt);
//    int result = -1;
//
//    if (status == SQLITE_ROW) {
//        result = sqlite3_column_int(stmt, 1);
//    } else if (status == SQLITE_DONE) {
//        // This doesn't make much sense?
//        fprintf(stderr, "Error: Failed to insert radical properly.\n");
//    }
//
//    sqlite3_reset(stmt);
//    return result;
//}

// Find by `key` returning id and (optionally) a given field.
// Return id on success, 0 if not found, and -1 if an error occurs.
static int32_t find_str(sqlite3_stmt *stmt, const char *key, int *field)
{
    if (sqlite_bind_str(stmt, 1, key)) {
        return -1;
    }

    int status = sqlite_step(stmt);
    int32_t result = -1;

    if (status == SQLITE_ROW) {
        result = sqlite3_column_int(stmt, 0);

        if (field) {
            (*field) = sqlite3_column_int(stmt, 1);
        }
    } else if (status == SQLITE_DONE) {
        result = 0;
    }

    sqlite3_reset(stmt);
    return result;
}

// Run insert statement, returning first int column and resetting properly.
static int32_t exec_insert_stmt(sqlite3_stmt *stmt, const char *thing)
{
    int status = sqlite3_step(stmt);
    int32_t result = -1;

    if (status == SQLITE_ROW) {
        result = sqlite3_column_int(stmt, 0);
    } else if (status == SQLITE_DONE) {
        // This shouldn't really happen in insert statements...
        fprintf(stderr, "Error: Error while inserting %s.\n", thing);
    }

    sqlite3_reset(stmt);
    return result;
}

// Handle single character dictionary entry. Return index on success, negative on failure.
static int32_t handle_char(struct sqlite_state *sqlite, struct charinfo info, struct insert_map *map)
{
    if (info.strokes_ext) {
        // This is a radical.
    } else {
        // This is not a radical.
    }

    // We can use this to determine if the saved value is a dummy
    int strokes;

    int32_t id = find_str(sqlite->char_find, info.str, &strokes);
    return exec_insert_stmt(sqlite->char_insert, "character");
}


// Find character info for word. Return index on success, negative on failure.
static int32_t word_charinfo(struct sqlite_state *sqlite, const char *chr)
{
    int32_t idx = find_str(sqlite->char_find, chr, NULL);
    if (idx) { return idx; }

    // Here, the character has not yet been accounted for. Make a dummy entry to be fixed later.
    if (sqlite_bind_str(sqlite->char_insert, SQL_INS_CHAR_CHAR,     chr)) { return -1; }
    if (sqlite_bind_int(sqlite->char_insert, SQL_INS_CHAR_RAD,      0))   { return -1; }
    if (sqlite_bind_int(sqlite->char_insert, SQL_INS_CHAR_STROKES,  0))   { return -1; }
    if (sqlite_bind_int(sqlite->char_insert, SQL_INS_CHAR_XSTROKES, 0))   { return -1; }
    if (sqlite_bind_str(sqlite->char_insert, SQL_INS_CHAR_ZHUYIN,   ""))  { return -1; }
    if (sqlite_bind_str(sqlite->char_insert, SQL_INS_CHAR_PINYIN,   ""))  { return -1; }
    if (sqlite_bind_str(sqlite->char_insert, SQL_INS_CHAR_XPRON,    ""))  { return -1; }
    if (sqlite_bind_int(sqlite->char_insert, SQL_INS_CHAR_PRON_ORD, 0))   { return -1; }

    return exec_insert_stmt(sqlite->char_insert, "dummy character");
}

// Build the map between sql params and excel columns
static int build_insert_map(struct xlsx *doc, struct xlsx_value *names, struct insert_map *map)
{
    // Initialize the map to indicate nothing has been found.
    for (int i = 0; i < SQL_INS_CHAR_CNT + 1; i++) {
        map->charmap[i] = -1;
    }

    for (int i = 0; i < SQL_INS_DICT_CNT + 1; i++) {
        map->dictmap[i] = -1;
    }

    // Just do 10 comparisons each time. It's ok.
    for (size_t i = 0; i < xlsx_cols(doc); i++)
    {
        // Column names should be strings.
        if (names[i].type != XLSX_TYPE_STR) {
            continue;
        }

        if (!strcmp(xlsx_str(doc, &names[i]), "字詞名")) {
            printf("Found '字詞名' --> %zu\n", i);

            map->charmap[SQL_INS_CHAR_CHAR] = i;
            map->dictmap[SQL_INS_DICT_WORD] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "字數")) {
            printf("Found '字數' --> %zu\n", i);
            map->dictmap[SQL_INS_DICT_CHARS] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "字詞號")) {
            printf("Found '字詞號' --> %zu\n", i);
            map->dictmap[SQL_INS_DICT_ID] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "部首字")) {
            printf("Found '部首字' --> %zu\n", i);
            map->charmap[SQL_INS_CHAR_RAD] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "總筆畫數")) {
            printf("Found '總筆畫數' --> %zu\n", i);
            map->charmap[SQL_INS_CHAR_STROKES] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "部首外筆畫數")) {
            printf("Found '部首外筆畫數' --> %zu\n", i);
            map->charmap[SQL_INS_CHAR_XSTROKES] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "注音一式")) {
            printf("Found '注音一式' --> %zu\n", i);
            map->charmap[SQL_INS_CHAR_ZHUYIN] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "漢語拼音")) {
            printf("Found '漢語拼音' --> %zu\n", i);
            map->charmap[SQL_INS_CHAR_PINYIN] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "釋義")) {
            printf("Found '釋義' --> %zu\n", i);
            map->dictmap[SQL_INS_DICT_DEF] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "多音參見訊息")) {
            printf("Found '多音參見訊息' --> %zu\n", i);
            map->charmap[SQL_INS_CHAR_XPRON] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "多音排序")) {
            printf("Found '多音排序' --> %zu\n", i);
            map->charmap[SQL_INS_CHAR_PRON_ORD] = i;
        }
    }

    // Make sure we got everything.
    for (size_t i = 1; i < SQL_INS_CHAR_CNT + 1; i++)
    {
        if (map->charmap[i] < 0)
        {
            fprintf(stderr, "Error: Missing column %zu\n", i);
            return 1;
        }
    }

    for (size_t i = 1; i < SQL_INS_DICT_CNT + 1; i++)
    {
        if (map->dictmap[i] < 0)
        {
            fprintf(stderr, "Error: Missing column %zu\n", i);
            return 1;
        }
    }

    printf("All columns mapped properly.\n");
    return 0;
}

// Insert everything in a single pass over the database
static int do_insert_pass(struct sqlite_state *sqlite, struct xlsx *doc, struct insert_map *map)
{
/*        #define do_bind_str(p, name)                                                            \
            do {                                                                                \
                struct xlsx_value *entry = &row[col_map[p]];                                    \
                const char *sval;                                                               \
                                                                                                \
                if (entry->type == XLSX_TYPE_STR) {                                             \
                    sval = xlsx_str(doc, entry);                                                \
                } else if (entry->type == XLSX_TYPE_LSTR) {                                     \
                    sval = entry->str;                                                          \
                } else if (entry->type == XLSX_TYPE_NULL) {                                     \
                    sval = NULL;                                                                \
                } else {                                                                        \
                    fprintf(stderr, "Error: " name " in row '%zu' is not a string!\n", i);      \
                    return 0;                                                                   \
                }                                                                               \
                                                                                                \
                if (sqlite_bind_str(insert_dict_stmt, p, sval)) {                               \
                    return 0;                                                                   \
                }                                                                               \
                                                                                                \
                if (p == ins_dict_char) {                                                       \
                    fprintf(stderr, "Preparing to insert '%s'...\n", xlsx_str(doc, entry));     \
                }                                                                               \
            } while (0)

//        #define do_bind_int(p, name)                                                                        \
            do {                                                                                            \
                struct xlsx_value *entry = &row[col_map[p]];                                                \
                size_t ival;                                                                                \
                                                                                                            \
                if (entry->type == XLSX_TYPE_STR || entry->type == XLSX_TYPE_LSTR) {                        \
                    const char *sval = (entry->type == XLSX_TYPE_STR) ? xlsx_str(doc, entry) : entry->str;  \
                    char *end; ival = strtoll(sval, &end, 10);                                              \
                                                                                                            \
                    if (end[0])                                                                             \
                    {                                                                                       \
                        fprintf(stderr, "Error: " name " (%s) in row '%zu' is malformed!\n", sval, i);      \
                        skipped++;                                                                          \
                                                                                                            \
                        return 1;                                                                           \
                    }                                                                                       \
                } else if (entry->type == XLSX_TYPE_INT) {                                                  \
                    ival = entry->ival;                                                                     \
                } else {                                                                                    \
                    fprintf(stderr, "Error: " name " in row '%zu' is not an int!\n", i);                    \
                }                                                                                           \
                                                                                                            \
                if (sqlite_bind_int(insert_dict_stmt, p, ival)) {                                           \
                    return 0;                                                                               \
                }                                                                                           \
                                                                                                            \
                if (p == ins_dict_xstr) {                                                                   \
                    if (ival == 0)                                                                          \
                    {                                                                                       \
                        strokes = ival;                                                                     \
                        is_rad = true;                                                                      \
                    }                                                                                       \
                } else if (p == ins_dict_cnt) {                                                             \
                    if (ival != 1)                                                                          \
                    {                                                                                       \
                        fprintf(stderr, "Skipping word.\n");                                                \
                        skipped++;                                                                          \
                                                                                                            \
                        return 1;                                                                           \
                    }                                                                                       \
                }                                                                                           \
            } while (0)*/

    // Cast an indexed value to a string, erroring out on failure.
    // Depends on `row`, `doc`, `i` external variables in the loop below.
    #define as_str_chk(idx, name) ({                                                                \
        struct xlsx_value *entry = &row[idx];                                                       \
        char *sval;                                                                                 \
                                                                                                    \
        if (entry->type == XLSX_TYPE_STR) {                                                         \
            sval = xlsx_str(doc, entry);                                                            \
        } else if (entry->type == XLSX_TYPE_LSTR) {                                                 \
            sval = entry->str;                                                                      \
        } else if (entry->type == XLSX_TYPE_NULL) {                                                 \
            sval = NULL;                                                                            \
        } else {                                                                                    \
            fprintf(stderr, "Error: " name " in row '%zu' is not a string!\n", i);                  \
            return -1;                                                                              \
        }                                                                                           \
                                                                                                    \
        sval;                                                                                       \
    })

    // Same as the string macro above, but not for integers.
    // Depends on `row`, `doc`, `i`, `skipped` external variables in the loop below.
    #define as_int_chk(idx, name) ({                                                                \
        struct xlsx_value *entry = &row[idx];                                                       \
        uint64_t ival;                                                                              \
                                                                                                    \
        if (entry->type == XLSX_TYPE_STR || entry->type == XLSX_TYPE_LSTR)  {                       \
            const char *sval = (entry->type == XLSX_TYPE_STR) ? xlsx_str(doc, entry) : entry->str;  \
            char *end; ival = strtoll(sval, &end, 10);                                              \
                                                                                                    \
            if (end[0])                                                                             \
            {                                                                                       \
                fprintf(stderr, "Error: " name " (%s) in row '%zu' is malformed!\n", sval, i);      \
                skipped++;                                                                          \
                                                                                                    \
                return 1;                                                                           \
            }                                                                                       \
        } else if (entry->type == XLSX_TYPE_INT) {                                                  \
            ival = entry->ival;                                                                     \
        } else {                                                                                    \
            fprintf(stderr, "Error: " name " in row '%zu' is not an int!\n", i);                    \
            return -1;                                                                              \
        }                                                                                           \
                                                                                                    \
        ival;                                                                                       \
    })

    // Some entries have weird numbers for some reason.
    // We skip them for now.
    __block int skipped = 0;

    // This is our insertion loop-- we go through the document once, inserting entries into all 3 tables.
    // All entries have a `dict` table entry, so we first find the char/word name and definition,
    //   along with the character count.
    // If there is only a single character, it gets an entry in the character table.
    // If there are no strokes outside the radical, it is a radical and gets an entry in the radical table.
    // If there are multiple characters, we look up the id for each of them and put it into character info for easy lookup.
    // At any point, we may have to put in dummy chars/radicals for other entries to reference.
    // Only the dictionary ids are actually preserved from the xlsx document.
    xlsx_foreach_row(doc, ^(struct xlsx_value *row, size_t i) {
        // Skip column headers
        if (!i) { return 1; }

        // Read info for next entry.
        struct dictinfo word = {
            .id = as_int_chk(map->dictmap[SQL_INS_DICT_ID], "Entry Number"),
            .str = as_str_chk(map->dictmap[SQL_INS_DICT_WORD], "Character/Word"),
            .definition = as_str_chk(map->dictmap[SQL_INS_DICT_DEF], "Definition"),
            .chars = as_int_chk(map->dictmap[SQL_INS_DICT_CHARS], "Character Count")
        };

        fprintf(stderr, "Preparing to insert '%s'...\n", word.str);

        // Buffer overflows are bad.
        if (word.chars > 6) {
            fprintf(stderr, "Error: '%s' in row %zu has too many characters! (max=6, found=%llu)\n", word.str, i, word.chars);
            return -1;
        } else if (!word.chars) {
            fprintf(stderr, "Warning: '%s' in row %zu has no characters?\n", word.str, i);
            return 1;
        }

        if (word.chars == 1) {
            // This is a single character.
            int char_id = handle_char(sqlite, ((struct charinfo){
                .str = as_str_chk(map->charmap[SQL_INS_CHAR_CHAR], "Character"),
                .rad = as_str_chk(map->charmap[SQL_INS_CHAR_RAD], "Radical"),
                .strokes = as_int_chk(map->charmap[SQL_INS_CHAR_STROKES], "Stroke Count"),
                .strokes_ext = as_int_chk(map->charmap[SQL_INS_CHAR_XSTROKES], "Stroke Count (- Radical)"),
                .zhuyin = as_str_chk(map->charmap[SQL_INS_CHAR_ZHUYIN], "Zhuyin"),
                .pinyin = as_str_chk(map->charmap[SQL_INS_CHAR_PINYIN], "Pinyin"),
                .pronoun_other = as_str_chk(map->charmap[SQL_INS_CHAR_XPRON], "Extra Pronunciation Info"),
                .pronoun_order = as_int_chk(map->charmap[SQL_INS_CHAR_PRON_ORD], "Prnounciation Order")
            }), map);

            if (char_id < 0) {
                return -1;
            }

            word.charinfo[0] = char_id;
        } else {
            // This is a multi-char entry
            // We need to copy out each char we will search for into a buffer.
            // We assume UTF-8, so 4 chars + a terminating \0
            uint8_t next[5] = { 0, 0, 0, 0, 0 };
            off_t offset = 0;

            for (size_t i = 0; i < word.chars; i++)
            {
                if (!word.str[offset])
                {
                    // The char count doesn't match the actual string length
                    fprintf(stderr, "Character count doesn't match word length!\n");
                    skipped++;

                    return 1;
                }

                // The first 8 bits determines how many bytes this char takes
                size_t bytes = UTF8_TRAILING_COUNT[(uint8_t)word.str[offset]];

                if (bytes > 3)
                {
                    // This is not a valid codepoint
                    fprintf(stderr, "Found invalid UTF-8 codepoint in word! (bytes=%zu)\n", bytes);
                    skipped++;

                    return 1;
                }

                memcpy(next, &word.str[offset], bytes + 1);
                next[bytes + 1] = 0;

                // Here, `next` holds the next single char.
                word.charinfo[i] = word_charinfo(sqlite, next);
            }
        }

        /*
        // Is the current entry a radical (extra stroke count == 0)
        bool is_rad = false;

        // # of strokes if this is a radical
        int strokes = 0;

        do_bind_str(ins_dict_char,  "Character");
        do_bind_int(ins_dict_cnt,   "Character count");
        do_bind_int(ins_dict_num,   "Entry number");
        // We need to compute this.
        //do_bind_int(ins_dict_rad, "Radical number");
        do_bind_int(ins_dict_str,   "Stroke count");
        do_bind_int(ins_dict_xstr,  "Extra stroke count");
        do_bind_str(ins_dict_prn,   "Pronunciation");
        do_bind_str(ins_dict_hpy,   "Latin pronunciation");
        do_bind_str(ins_dict_def,   "Definition");
        do_bind_str(ins_dict_xprn,  "Extra pronunciations");
        do_bind_int(ins_dict_xprno, "Extra pronunciation order");

        struct xlsx_value *rentry = &row[col_map[ins_dict_rad]];

        if (rentry->type != XLSX_TYPE_STR)
        {
            fprintf(stderr, "Error: Radical in row '%zu' is not a string!\n", i);
            return 0;
        }

        // Radical string value
        const char *rad = xlsx_str(doc, rentry);

        // Radical stored stroke count
        int rstrokes = 0;

        // Get index of radical entry if it exists.
        int rindex = do_find_rad(find_rad_stmt, rad, &rstrokes);
        if (rindex < 0) { return 0; }

        if (rindex) {
            // If this is a radical entry, we may need to update stroke count (if inserted early)
            if (is_rad && !rstrokes)
            {
                // Use the previously saved stroke count; do update.
                if (do_update_rad(update_rad_stmt, rindex, strokes)) {
                    return 0;
                }
            }
        } else {
            // No row exists for this radical yet. We have to make one here.

            // If this happens to be the radical itself, we also know the stroke count, otherwise use 0 as a placeholder.
            // (We always know the radical char itself, of course)
            rindex = do_insert_rad(insert_rad_stmt, rad, is_rad ? strokes : 0);
            if (rindex < 0) { return 0; }
        }

        // At this point, we know the index to use.
        if (sqlite_bind_int(insert_dict_stmt, ins_dict_rad, rindex)) { return 0; }

        // And now we've bound everything for the dictionary insert.
        if (sqlite_step(insert_dict_stmt) != SQLITE_DONE) { return 0; }
        sqlite3_reset(insert_dict_stmt);*/

        return 1;
    });

    return -1;

    #undef do_bind_str
    #undef do_bind_int
}

int main(int argc, const char *const *argv)
{
    const char *xlsx_path = NULL;
    const char *db_path = NULL;

    if (argc == 4) {
        if (strcmp(argv[1], "-f"))
        {
            fprintf(stderr, "Error: Invalid 1st argument '%s'\n", argv[1]);
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
        fprintf(stderr, "Error: Need 2 or 3 arguments.\n");
        return 1;
    }

    // Open dictionary data xlsx document
    struct xlsx *doc = xlsx_doc_at(xlsx_path);
    if (!doc) { return 1; }

    if (!xlsx_rows(doc) || !xlsx_cols(doc))
    {
        fprintf(stderr, "Error: Dictionary sheet is empty!\n");
        xlsx_doc_free(doc);

        return 1;
    }

    // Setup database with tables + prepared statements.
    struct sqlite_state sqlite;

    if (sqlite_setup(&sqlite, db_path))
    {
        fprintf(stderr, "Error: Failed to setup database (at '%s').\n", db_path);
        xlsx_doc_free(doc);

        return 1;
    }

    // Create mapping of row # --> insert query parameter number
    struct insert_map insert_map;

    if (build_insert_map(doc, xlsx_row(doc, 0), &insert_map))
    {
        sqlite_destroy(&sqlite, true);
        xlsx_doc_free(doc);

        return 1;
    }

    int ok = do_insert_pass(&sqlite, doc, &insert_map);
    sqlite_destroy(&sqlite, !!ok);
    xlsx_doc_free(doc);

    if (ok) {
        fprintf(stderr, "Finished inserting entries from xlsx doc.\n");
    } else {
        fprintf(stderr, "Encountered errors while inserting entries.\n");
    }

    return ok;
}
