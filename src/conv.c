// Convert dictionary data from XLSX document to sqlite database
#include <strings.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <sqlite.h>
#include <xlsx.h>

// Macro value --> string value (for param # macros)
#define _STR(v) #v
#define STR(n) _STR(n)

// Make radical and dictionary tables in a given database.
static int make_tables(sqlite3 *db)
{
    return sqlite_exec(db, "create table 部首 ("
        "編號 integer primary key, "
        "字 text, "
        "筆畫數 integer"
    ") strict;"

    "create table 辭典 ("
        "字詞名 text, "
        "字數 integer, "
        "編號 integer primary key, "
        "部首 integer references 部首 (id), "
        "筆畫數 integer, "
        "部首外筆畫數 integer, "
        "注音 text, "
        "漢拼 text, "
        "釋義資料 text, "
        "多音資料 text, "
        "多音排序 integer"
    ") strict;"

    "create index ientries on 辭典 (編號);", NULL);
}

static sqlite3_stmt *make_insert_rad(sqlite3 *db)
{
    printf("Prepare insert radical statement...\n");

    #define ins_rad_char    1
    #define ins_rad_str     2

    return sqlite_prepare(db, "insert into 部首 ("
        "字, 筆畫數"
    ") values("
        "?" STR(ins_rad_char) ", " /* 字 */
        "?" STR(ins_rad_str)       /* 筆畫數 */
    ") returning 編號;");
}

static sqlite3_stmt *make_insert_dict(sqlite3 *db)
{
    printf("Prepare insert dictionary statement...\n");

    #define INS_COL_CNT     11

    #define ins_dict_char   1
    #define ins_dict_cnt    2
    #define ins_dict_num    3
    #define ins_dict_rad    4
    #define ins_dict_str    5
    #define ins_dict_xstr   6
    #define ins_dict_prn    7
    #define ins_dict_hpy    8
    #define ins_dict_def    9
    #define ins_dict_xprn   10
    #define ins_dict_xprno  11

    return sqlite_prepare(db, "insert into 辭典 values("
        "?" STR(ins_dict_char)  ", " /* 字詞名 */
        "?" STR(ins_dict_cnt)   ", " /* 字數 */
        "?" STR(ins_dict_num)   ", " /* 編號 */
        "?" STR(ins_dict_rad)   ", " /* 部首 */
        "?" STR(ins_dict_str)   ", " /* 筆畫數 */
        "?" STR(ins_dict_xstr)  ", " /* 部首外筆畫數 */
        "?" STR(ins_dict_prn)   ", " /* 注音 */
        "?" STR(ins_dict_hpy)   ", " /* 漢拼 */
        "?" STR(ins_dict_def)   ", " /* 釋義資料 */
        "?" STR(ins_dict_xprn)  ", " /* 多音資料 */
        "?" STR(ins_dict_xprno)      /* 多音排序 */
    ");");
}

static sqlite3_stmt *make_update_rad(sqlite3 *db)
{
    printf("Prepare update radical statement...\n");

    #define update_rad_str 1
    #define update_rad_idx 2

    return sqlite_prepare(db, "update 部首 "
        "set 筆畫數 = ?" STR(update_rad_str)
        " where 編號 = ?" STR(update_rad_idx)
    ";");
}

static sqlite3_stmt *make_find_rad(sqlite3 *db)
{
    printf("Prepare find radical statement...\n");

    return sqlite_prepare(db, "select 編號, 筆畫數 from 部首 where 字 = ?;");
}

// Find a radical, returning (non-zero) row offset if found, zero if not found, and negative if an error occurred
static int do_find_rad(sqlite3_stmt *stmt, const char *rad, int *strokes)
{
    if (sqlite_bind_str(stmt, 1, rad)) {
        return -1;
    }

    int status = sqlite_step(stmt);
    int result = -1;

    if (status == SQLITE_ROW) {
        result = sqlite3_column_int(stmt, 0);

        // Also give stroke count.
        if (strokes) {
            (*strokes) = sqlite3_column_int(stmt, 1);
        }
    } else if (status == SQLITE_DONE) {
        result = 0;
    }

    sqlite3_reset(stmt);
    return result;
}

// Update the radical at `index` to have `strokes` strokes.
static int do_update_rad(sqlite3_stmt *stmt, int index, int strokes)
{
    if (sqlite_bind_int(stmt, update_rad_str, strokes)) { return 1; }
    if (sqlite_bind_int(stmt, update_rad_idx, index  )) { return 1; }

    int result = sqlite_step(stmt);
    sqlite3_reset(stmt);

    return (result != SQLITE_DONE);
}

// Insert new radical with info, returning inserted index on success, negative on failure.
static int do_insert_rad(sqlite3_stmt *stmt, const char *rad, int strokes)
{
    printf("Insert new radical '%s'\n", rad);

    if (sqlite_bind_str(stmt, ins_rad_char, rad    )) { return -1; }
    if (sqlite_bind_int(stmt, ins_rad_str,  strokes)) { return -1; }

    int status = sqlite3_step(stmt);
    int result = -1;

    if (status == SQLITE_ROW) {
        result = sqlite3_column_int(stmt, 1);
    } else if (status == SQLITE_DONE) {
        // This doesn't make much sense?
        fprintf(stderr, "Error: Failed to insert radical properly.\n");
    }

    sqlite3_reset(stmt);
    return result;
}

// Return a buffer of length INS_COL_CNT + 1 on the stack mapping parameter numbers to column numbers.
static off_t *make_column_map(struct xlsx *doc, struct xlsx_value *names)
{
    off_t col_map[INS_COL_CNT + 1] = {
        -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1
    };

    // Just do 10 comparisons each time. It's ok.
    for (size_t i = 0; i < xlsx_cols(doc); i++)
    {
        if (names[i].type != XLSX_TYPE_STR) {
            continue;
        }

        if (!strcmp(xlsx_str(doc, &names[i]), "字詞名")) {
            printf("Found '字詞名' --> %zu\n", i);
            col_map[ins_dict_char] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "字數")) {
            printf("Found '字數' --> %zu\n", i);
            col_map[ins_dict_cnt] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "字詞號")) {
            printf("Found '字詞號' --> %zu\n", i);
            col_map[ins_dict_num] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "部首字")) {
            printf("Found '部首字' --> %zu\n", i);
            col_map[ins_dict_rad] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "總筆畫數")) {
            printf("Found '總筆畫數' --> %zu\n", i);
            col_map[ins_dict_str] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "部首外筆畫數")) {
            printf("Found '部首外筆畫數' --> %zu\n", i);
            col_map[ins_dict_xstr] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "注音一式")) {
            printf("Found '注音一式' --> %zu\n", i);
            col_map[ins_dict_prn] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "漢語拼音")) {
            printf("Found '漢語拼音' --> %zu\n", i);
            col_map[ins_dict_hpy] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "釋義")) {
            printf("Found '釋義' --> %zu\n", i);
            col_map[ins_dict_def] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "多音參見訊息")) {
            printf("Found '多音參見訊息' --> %zu\n", i);
            col_map[ins_dict_xprn] = i;
        } else if (!strcmp(xlsx_str(doc, &names[i]), "多音排序")) {
            printf("Found '多音排序' --> %zu\n", i);
            col_map[ins_dict_xprno] = i;
        }
    }

    for (size_t i = 1; i < INS_COL_CNT; i++)
    {
        if (col_map[i] < 0)
        {
            fprintf(stderr, "Error: Missing column %zu\n", i);
            return NULL;
        }
    }

    printf("All columns mapped properly.\n");

    // Copy to a heap allocated buffer for return.
    off_t *result = malloc((INS_COL_CNT + 1) * sizeof(off_t));

    if (!result)
    {
        perror("malloc");
        return NULL;
    }

    memcpy(result, col_map, (INS_COL_CNT + 1) * sizeof(off_t));
    return result;
}

int main(int argc, const char *const *argv)\
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

    // Open dictionaryd data xlsx document
    struct xlsx *doc = xlsx_doc_at(xlsx_path);
    if (!doc) { return 1; }

    if (!xlsx_rows(doc) || !xlsx_cols(doc))
    {
        fprintf(stderr, "Error: Dictionary sheet is empty!\n");
        xlsx_doc_free(doc);

        return 1;
    }

    // Open sqlite database to create.
    sqlite3 *db = sqlite_open(db_path, false);

    if (!db)
    {
        xlsx_doc_free(doc);
        return 1;
    }

    if (make_tables(db))
    {
        fprintf(stderr, "Error: Failed to make tables.\n");

        xlsx_doc_free(doc);
        sqlite_close(db);

        if (unlink(db_path))
        { perror("unlink"); }

        return 1;
    }

    sqlite3_stmt *insert_rad_stmt = make_insert_rad(db);
    sqlite3_stmt *insert_dict_stmt = make_insert_dict(db);
    sqlite3_stmt *update_rad_stmt = make_update_rad(db);
    sqlite3_stmt *find_rad_stmt = make_find_rad(db);

    if (!insert_rad_stmt || !insert_dict_stmt || !update_rad_stmt || !find_rad_stmt)
    {
        fprintf(stderr, "Error: Failed to create prepared statements.\n");

        xlsx_doc_free(doc);
        sqlite_close(db);

        return 1;
    }

    // Create mapping of row # --> insert query parameter number
    __block off_t *col_map = make_column_map(doc, xlsx_row(doc, 0));

    if (!col_map)
    {
        xlsx_doc_free(doc);
        sqlite_close(db);

        return 1;
    }

    // Some entries have weird numbers for some reason.
    // We skip them for now.
    __block int skipped = 0;

    xlsx_foreach_row(doc, ^(struct xlsx_value *row, size_t i) {
        if (!i) { return 1; }

        #define do_bind_str(p, name)                                                            \
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

        #define do_bind_int(p, name)                                                                        \
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
                /* This conditional is optimized away. */                                                   \
                if (p == ins_dict_xstr) {                                                                   \
                    if (ival == 0)                                                                          \
                    {                                                                                       \
                        strokes = ival;                                                                     \
                        is_rad = true;                                                                      \
                    }                                                                                       \
                } else if (p == ins_dict_cnt) {                                                             \
                    if (ival != 1)\
                    {\
                        fprintf(stderr, "Skipping word.\n");\
                        skipped++;\
                        \
                        return 1;\
                    }\
                }\
            } while (0)

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

        #undef do_bind_str
        #undef do_bind_int

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
        sqlite3_reset(insert_dict_stmt);

        return 1;
    });

    fprintf(stderr, "Finished inserting entries from xlsx doc.\n");

    free(col_map);

    xlsx_doc_free(doc);
    sqlite_close(db);

    return 0;
}
