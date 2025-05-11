/* ********************************************************** */
/* -*- xldict.c -*- Query xlsx dictionary                 -*- */
/* ********************************************************** */
/* Tyler Besselman (C) August 2024                            */
/* ********************************************************** */

#include <strings.h>
#include <stdbool.h>

#include <xlsx.h>

static int do_query(struct xlsx *doc, const char *query, off_t names, off_t defs)
{
    __block unsigned int matches = 0;

    xlsx_iter_col(doc, names, ^(struct xlsx_value *val, size_t row) {
        if (val->type != XLSX_TYPE_STR)
        {
            fprintf(stderr, "Error: Entry is not a string!\n");
            return false;
        }

        const char *name = xlsx_str(doc, val);

        if (!strcmp(name, query))
        {
            matches++;

            struct xlsx_value *info = xlsx_row(doc, row);
            if (!info) { return false; }

            printf("Found '%s' at %zu.\n", query, row + 1);

            struct xlsx_value *def = &info[defs];
            if (!def) { return true; }

            if (def->type != XLSX_TYPE_STR) {
                fprintf(stderr, "Error: Definition is not of string type! (type=%d)\n", def->type);
            } else {
                printf("Definition %u:\n%s\n", matches, xlsx_str(doc, def));
            }
        }

        return true;
    });

    return !!matches;
}

int main(int argc, const char *const *argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Error: Need exactly 1 argument.\n");
        return 1;
    }

    struct xlsx *doc = xlsx_doc_at(argv[1]);
    if (!doc) { return 1; }

    off_t names = -1;
    off_t defs = -1;

    struct xlsx_value *header = xlsx_row(doc, 0);

    for (size_t i = 0; i < xlsx_cols(doc); i++)
    {
        struct xlsx_value *val = &header[i];

        if (val->type != XLSX_TYPE_STR)
        {
            fprintf(stderr, "Error: Column header is not a string! (type=%d)\n", val->type);
            continue;
        }

        printf("%zu: '%s'\n", i, xlsx_str(doc, val));

        if (!strcmp("字詞名", xlsx_str(doc, val))) {
            names = i;
        } else if (!strcmp("釋義", xlsx_str(doc, val))) {
            defs = i;
        }
    }

    if ((names < 0) || (defs < 0))
    {
        fprintf(stderr, "Error: Missing names or definitions.\n");
        return 1;
    }

    char buf[16];
    char *str;

    printf("Enter query: ");

    while ((str = fgets(buf, 16, stdin)))
    {
        // Remove trailing newline.
        size_t len = strlen(str);
        str[len - 1] = 0;

        // Do query.
        printf("Looking for '%s'...\n", str);

        if (do_query(doc, str, names, defs)) {
            printf("No records found.\n");
        }

        printf("Enter query: ");
    }

    xlsx_doc_free(doc);
    return 0;
}
