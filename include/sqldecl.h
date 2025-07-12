/* ********************************************************** */
/* -*- sqldecl.c -*- SQL declarations for sqlite dict     -*- */
/* ********************************************************** */
/* Tyler Besselman (C) May 2025                               */
/* ********************************************************** */

#ifndef __SQLDECL__
#define __SQLDECL__ 1

// Macro value --> string value conversion (for parameter #s)
#define __SQLSTR(v) #v
#define _SQLSTR(n) __SQLSTR(n)

// Name strings for radical table
#define SQL_TABLE_RAD_NAME              "部首"
#define SQL_TABLE_RAD_FIELD_ID          "編號"
#define SQL_TABLE_RAD_FIELD_CHAR        "字"
#define SQL_TABLE_RAD_FIELD_STROKES     "筆畫數"

// Name strings for character table
#define SQL_TABLE_CHAR_NAME             "字"
#define SQL_TABLE_CHAR_FIELD_ID         "編號"
#define SQL_TABLE_CHAR_FIELD_CHAR       "字"
#define SQL_TABLE_CHAR_FIELD_RAD        "部首"
#define SQL_TABLE_CHAR_FIELD_STROKES    "筆畫數"
#define SQL_TABLE_CHAR_FIELD_XSTROKES   "部首外筆畫數"
#define SQL_TABLE_CHAR_FIELD_ZHUYIN     "注音"
#define SQL_TABLE_CHAR_FIELD_PINYIN     "漢拼"
#define SQL_TABLE_CHAR_FIELD_XPRON      "多音資料"
#define SQL_TABLE_CHAR_FIELD_PRON_ORD   "多音排序"

// Name strings for word (dict) table
#define SQL_TABLE_DICT_NAME             "辭典"
#define SQL_TABLE_DICT_FIELD_ID         "編號"
#define SQL_TABLE_DICT_FIELD_WORD       "字詞"
#define SQL_TABLE_DICT_FIELD_CHARS      "詞"
#define SQL_TABLE_DICT_FIELD_CHAR_INFO  "詞"
#define SQL_TABLE_DICT_FIELD_DEF        "釋義資料"

// SQL creation statement for radical table
#define SQL_STMT_CREATE_RAD                                                             \
    "create table " SQL_TABLE_RAD_NAME "("                                              \
        SQL_TABLE_RAD_FIELD_ID      " integer primary key, "                            \
        SQL_TABLE_RAD_FIELD_CHAR    " text not null, "                                  \
        SQL_TABLE_RAD_FIELD_STROKES " integer"                                          \
    ") strict;"

// SQL creation statement for character table
#define SQL_STMT_CREATE_CHAR                                                            \
    "create table" SQL_TABLE_CHAR_NAME "("                                              \
        SQL_TABLE_CHAR_FIELD_ID         " integer primary key, "                        \
        SQL_TABLE_CHAR_FIELD_CHAR       " text not null, "                              \
        "foreign key(" SQL_TABLE_CHAR_FIELD_RAD ") "                                    \
            "references "   SQL_TABLE_RAD_NAME "(" SQL_TABLE_RAD_FIELD_ID "), "         \
        SQL_TABLE_CHAR_FIELD_STROKES    " integer, "                                    \
        SQL_TABLE_CHAR_FIELD_XSTROKES   " integer, "                                    \
        SQL_TABLE_CHAR_FIELD_ZHUYIN     " text, "                                       \
        SQL_TABLE_CHAR_FIELD_PINYIN     " text, "                                       \
        SQL_TABLE_CHAR_FIELD_XPRON      " text, "                                       \
        SQL_TABLE_CHAR_FIELD_PRON_ORD   " integer"                                      \
    ") strict;"

// SQL creation statement for dictionary table
#define SQL_STMT_CREATE_DICT                                                            \
    "create table " SQL_TABLE_DICT_NAME "("                                             \
        SQL_TABLE_DICT_FIELD_ID         " integer primary key, "                        \
        SQL_TABLE_DICT_FIELD_WORD       " text not null, "                              \
        SQL_TABLE_DICT_FIELD_CHARS      " integer, "                                    \
        SQL_TABLE_DICT_FIELD_CHAR_INFO  " blob,"                                        \
        SQL_TABLE_DICT_FIELD_DEF        " text not null"                                \
    ") strict;"

// SQL creation statement for table indicies
#define SQL_STMT_CREATE_INDEX                                                           \
    "create index irad      on " SQL_TABLE_RAD_NAME  "(" SQL_TABLE_RAD_FIELD_CHAR  ");" \
    "create index ichars    on " SQL_TABLE_CHAR_NAME "(" SQL_TABLE_CHAR_FIELD_CHAR ");" \
    "create index ientries  on " SQL_TABLE_DICT_NAME "(" SQL_TABLE_DICT_FIELD_WORD ");"

// Parameter count for radical insertion statement
#define SQL_INS_RAD_CNT         2

// Individual parameter numbers for radical insertion statement
#define SQL_INS_RAD_CHAR        1
#define SQL_INS_RAD_STROKES     2

// SQL statement for inserting into radical table
#define SQL_STMT_INSERT_RAD                                                 \
    "insert into " SQL_TABLE_RAD_NAME " ("                                  \
        SQL_TABLE_RAD_FIELD_CHAR ", "                                       \
        SQL_TABLE_RAD_FIELD_STROKES                                         \
    ") values("                                                             \
        "?" _SQLSTR(SQL_INS_RAD_CHAR) ", "                                  \
        "?" _SQLSTR(SQL_INS_RAD_STROKES)                                    \
    ") returning " SQL_TABLE_RAD_FIELD_ID ";"

// Parameter count for character insertion statement
#define SQL_INS_CHAR_CNT        8

// Individual parameter numbers for character insertion statement
#define SQL_INS_CHAR_CHAR       1
#define SQL_INS_CHAR_RAD        2
#define SQL_INS_CHAR_STROKES    3
#define SQL_INS_CHAR_XSTROKES   4
#define SQL_INS_CHAR_ZHUYIN     5
#define SQL_INS_CHAR_PINYIN     6
#define SQL_INS_CHAR_XPRON      7
#define SQL_INS_CHAR_PRON_ORD   8

// SQL statement for inserting into character table
#define SQL_STMT_INSERT_CHAR                                                \
    "insert into " SQL_TABLE_CHAR_NAME " ("                                 \
        SQL_TABLE_CHAR_FIELD_CHAR       ", "                                \
        SQL_TABLE_CHAR_FIELD_RAD        ", "                                \
        SQL_TABLE_CHAR_FIELD_STROKES    ", "                                \
        SQL_TABLE_CHAR_FIELD_XSTROKES   ", "                                \
        SQL_TABLE_CHAR_FIELD_ZHUYIN     ", "                                \
        SQL_TABLE_CHAR_FIELD_PINYIN     ", "                                \
        SQL_TABLE_CHAR_FIELD_XPRON      ", "                                \
        SQL_TABLE_CHAR_FIELD_PRON_ORD                                       \
    ") values("                                                             \
        "?" _SQLSTR(SQL_INS_CHAR_ID)        ", "                            \
        "?" _SQLSTR(SQL_INS_CHAR_CHAR)      ", "                            \
        "?" _SQLSTR(SQL_INS_CHAR_RAD)       ", "                            \
        "?" _SQLSTR(SQL_INS_CHAR_STROKES)   ", "                            \
        "?" _SQLSTR(SQL_INS_CHAR_XSTROKES)  ", "                            \
        "?" _SQLSTR(SQL_INS_CHAR_ZHUYIN)    ", "                            \
        "?" _SQLSTR(SQL_INS_CHAR_PINYIN)    ", "                            \
        "?" _SQLSTR(SQL_INS_CHAR_XPRON)     ", "                            \
        "?" _SQLSTR(SQL_INS_CHAR_PRON_ORD)                                  \
    ") returning " SQL_TABLE_CHAR_FIELD_ID ";"

// Parameter count for dictionary insertion statement
#define SQL_INS_DICT_CNT        5

// Individual parameter numbers for dictionary insertion statement
#define SQL_INS_DICT_ID         1
#define SQL_INS_DICT_WORD       2
#define SQL_INS_DICT_CHARS      3
#define SQL_INS_DICT_CHAR_INFO  4
#define SQL_INS_DICT_DEF        5

// SQL statement for inserting into dictionary table
#define SQL_STMT_INSERT_DICT                                                \
    "insert into " SQL_TABLE_DICT_NAME " ("                                 \
        SQL_TABLE_DICT_FIELD_ID         ", "                                \
        SQL_TABLE_DICT_FIELD_WORD       ", "                                \
        SQL_TABLE_DICT_FIELD_CHARS      ", "                                \
        SQL_TABLE_DICT_FIELD_CHAR_INFO  ", "                                \
        SQL_TABLE_DICT_FIELD_DEF                                            \
    ") values("                                                             \
        "?" _SQLSTR(SQL_INS_DICT_ID)        ", "                            \
        "?" _SQLSTR(SQL_INS_DICT_WORD)      ", "                            \
        "?" _SQLSTR(SQL_INS_DICT_CHARS)     ", "                            \
        "?" _SQLSTR(SQL_INS_DICT_CHAR_INFO) ", "                            \
        "?" _SQLSTR(SQL_INS_DICT_DEF)                                       \
    ") returning " SQL_TABLE_DICT_FIELD_ID ";"

// Parameter count for radical update statement
#define SQL_UPD_RAD_CND         2

// Individual parameter numbers for radical update
#define SQL_UPD_RAD_ID          1
#define SQL_UPD_RAD_STROKES     2

// SQL statement for updating radical entries
#define SQL_STMT_UPDATE_RAD                                                 \
    "update " SQL_TABLE_RAD_NAME " set "                                    \
        SQL_TABLE_RAD_FIELD_STROKES " = ?" _SQLSTR(SQL_UPD_RAD_STROKES)     \
    "where " SQL_TABLE_RAD_FIELD_ID " = ?" _SQLSTR(SQL_UPD_RAD_ID) ";"

// Parameter count for character update statement
#define SQL_UPD_CHAR_CND        4

// Individual parameter numbers for character update
#define SQL_UPD_CHAR_ID         1
#define SQL_UPD_CHAR_RAD        2
#define SQL_UPD_CHAR_STROKES    3
#define SQL_UPD_CHAR_XSTROKES   4
#define SQL_UPD_CHAR_ZHUYIN     5
#define SQL_UPD_CHAR_PINYIN     6
#define SQL_UPD_CHAR_XPRON      7
#define SQL_UPD_CHAR_PRON_ORD   8

// SQL statement for updating character entries
#define SQL_STMT_UPDATE_CHAR                                                \
    "update " SQL_TABLE_CHAR_NAME " set ("                                  \
        SQL_TABLE_CHAR_FIELD_RAD        ", "                                \
        SQL_TABLE_CHAR_FIELD_STROKES    ", "                                \
        SQL_TABLE_CHAR_FIELD_XSTROKES   ", "                                \
        SQL_TABLE_CHAR_FIELD_ZHUYIN     ", "                                \
        SQL_TABLE_CHAR_FIELD_PINYIN     ", "                                \
        SQL_TABLE_CHAR_FIELD_XPRON      ", "                                \
        SQL_TABLE_CHAR_FIELD_PRON_ORD                                       \
    ") = ("                                                                 \
        "?" _SQLSTR(SQL_UPD_CHAR_RAD)       ", "                            \
        "?" _SQLSTR(SQL_UPD_CHAR_STROKES)   ", "                            \
        "?" _SQLSTR(SQL_UPD_CHAR_XSTROKES)  ", "                            \
        "?" _SQLSTR(SQL_UPD_CHAR_ZHUYIN)    ", "                            \
        "?" _SQLSTR(SQL_UPD_CHAR_PINYIN)    ", "                            \
        "?" _SQLSTR(SQL_UPD_CHAR_XPRON)     ", "                            \
        "?" _SQLSTR(SQL_UPD_CHAR_PRON_ORD)                                  \
    ") where " SQL_TABLE_CHAR_FIELD_ID " = ?" _SQLSTR(SQL_UPD_CHAR_ID) ";"

#endif /* !defined(__SQLDECL__) */
