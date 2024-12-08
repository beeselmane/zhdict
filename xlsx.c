#include <strings.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "xml.h"

// "rels" file stores some info on how to access worksheet data.
#define XLSX_RELS "xl/_rels/workbook.xml.rels"

char *xlsx_rpath(const char *xlsx_root, size_t len)
{
    size_t buflen = len + strlen(XLSX_RELS) + 2;
    char *buf = malloc(buflen);

    if (!buf)
    {
        perror("malloc");
        return NULL;
    }

    strlcpy(buf, xlsx_root, len + 1);
    strlcpy(&buf[len], XLSX_RELS, strlen(XLSX_RELS) + 1);

    char *res = realpath(buf, NULL);
    if (!res) { perror("realpath"); }

    free(buf);
    return res;
}



int main(int argc, const char *const *argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Error: Need exactly 1 argument.\n");
        return 1;
    }

    const char *root = argv[1];
    int error = 0;

    // XLSX files are glorified zip archives.
    zip_t *archive = zip_open(root, ZIP_RDONLY, &error);

    if (!archive)
    {
        _zerror("zip_open", error);
        return 1;
    }

    xmlNodePtr rels = zxml_root_at(archive, XLSX_RELS);

    if (!rels)
    {
        zclose(archive);
        return 1;
    }

    // Find here really just checks and makes sure the root name is correct.
    xmlNodePtr rdata = xml_find(rels, "Relationships");

    xml_dump_tree(rdata);

    xmlFreeDoc(rels->doc);
    zclose(archive);

    return 0;
}
