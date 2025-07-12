/* ********************************************************** */
/* -*- cmd.c -*- Source for some basic XML/XLSX utilities -*- */
/* ********************************************************** */
/* Tyler Besselman (C) December 2024                          */
/* ********************************************************** */

#include <stdbool.h>
#include <stdio.h>

#include <xlsx.h>
#include <xml.h>

#ifdef __XLSX_STANDALONE__
    // Test main function which reads in an XLSX file and dumps it in a grid.
    int main(int argc, const char *const *argv)
    {
        if (argc != 2)
        {
            fprintf(stderr, "Error: Need exactly 1 argument.\n");
            return 1;
        }

        struct xlsx *document = xlsx_doc_at(argv[1]);
        if (!document) { return 1; }

        printf("%4s", "");

        for (size_t i = 0; i < xlsx_cols(document); i++) {
            printf("%*s%03zu", 13, "C", i);
        }

        putchar('\n');

        xlsx_foreach_row(document, ^(struct xlsx_value *row, size_t n) {
            printf("R%03zu", n);

            for (size_t col = 0; col < xlsx_cols(document); col++)
            {
                struct xlsx_value *value = &row[col];

                switch (value->type)
                {
                    case XLSX_TYPE_NULL:  printf("%16s", "");                        break;
                    case XLSX_TYPE_STR:   printf("%16s", xlsx_str(document, value)); break;
                    case XLSX_TYPE_INT:   printf("%16lld", value->ival);             break;
                    case XLSX_TYPE_FLOAT: printf("%16lf", value->fval);              break;
                    case XLSX_TYPE_LSTR:  printf("%16s", value->str);                break;
                }
            }

            putchar('\n');
            return 0;
        });

        xlsx_doc_free(document);
        return 0;
    }
#endif /* defined(__XLSX_STANDALONE__) */

#ifdef __XML_STANDALONE__
    // Test main function which dumps a passed XML file.
    int main(int argc, const char *const *argv)
    {
        if (argc != 2)
        {
            fprintf(stderr, "Error: Need exactly 1 argument.\n");
            return 1;
        }

        xmlNodePtr root = xml_root_at(argv[1]);
        if (!root) { return 1; }

        xml_dump_tree(root);

        xmlFreeDoc(root->doc);
        return 0;
    }
#endif /* defined(__XML_STANDALONE__) */

#ifdef __ZXML_STANDALONE__
    // Small tool to dump xml at a given path in a zip archive.
    int main(int argc, const char *const *argv)
    {
        if (argc != 3)
        {
            fprintf(stderr, "Error: Need exactly 2 arguments.\n");
            return 1;
        }

        zip_t *archive = zopen(argv[1]);
        if (!archive) { return 1; }

        xmlNodePtr root = zxml_root_at(archive, argv[2]);
        if (!root) { return 1; }

        xml_dump_tree(root);

        zclose(archive);
        xmlFreeDoc(root->doc);

        return 0;
    }
#endif /* defined(__ZXML_STANDALONE__) */
