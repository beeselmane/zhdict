#include <strings.h>
#include <stdbool.h>
#include <stdlib.h>

#include "xml.h"

#define DEBUG_XLSX 1

// "rels" file stores some info on how to access worksheet data.
#define XLSX_RELS "xl/_rels/workbook.xml.rels"

// Dataset row entry value
struct xlsx_value {
    enum {
        XLSX_TYPE_STR,
        XLSX_TYPE_INT,
        XLSX_TYPE_FLOAT
    } type;

    union {
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

};

// Given a path, make it relative to the `xl` directory
// The returned path should be passed to `free`
static char *_xlsx_xl_path(const char *path)
{
    size_t len = strlen(path);
    void *buf;

    if (!strcmp("../", path)) {
        // This path should not be relative to `xl`
        buf = malloc(len - 2);
        if (!buf) { goto mfail; }

        strlcpy(buf, &path[3], len - 2);
    } else {
        // This path should be.
        buf = malloc(len + 4);
        if (!buf) { goto mfail; }

        memcpy(buf, "xl/", 3);
        strlcpy(&buf[3], path, len + 1);
    }

    if (DEBUG_XLSX) {
        printf("'%s' --> '%s'\n", path, (char *)buf);
    }

    return buf;

mfail:
    perror("malloc");
    return NULL;
}

// Given a path, open the `xl` relative version, handling errors properly.
static xmlNodePtr _xlsx_xl_root(zip_t *archive, const char *path)
{
    char *xl_path = _xlsx_xl_path(path);
    if (!xl_path) { return NULL; }

    xmlNodePtr root = zxml_root_at(archive, xl_path);
    free(xl_path);

    if (!root)
    {
        zclose(archive);
        return NULL;
    }

    return root;
}

// Build a string table from the XML file at the given (xl-rel) path in an archive.
static int _xlsx_strtab(zip_t *archive, const char *path, struct xlsx_strtab *strtab)
{
    xmlNodePtr strdata = _xlsx_xl_root(archive, path);
    if (!strdata) { return 1; }

    // This is similar to the find we do on reldata. Just check the first name is ok.
    xmlNodePtr table = xml_find(strdata, "sst");

    if (!table)
    {
        fprintf(stderr, "Error: Excel document has malformed strings table!\n");

        xmlFreeDoc(strdata->doc);
        zclose(archive);

        return 1;
    }

    // Initialize some things.
    strtab->ref = table->doc;
    strtab->base = NULL;
    strtab->count = 0;

    // We're going to copy pointers to the internal memory of the `strdata` document.
    // Otherwise we have to reallocate things, which just seems wasteful.
    // The table node should include a `count` attribute telling us how many strings there are.
    xml_node_attributes(table, ^(xmlAttrPtr attr, size_t _) {
        if (!strcmp("count", (const char *)attr->name)) {
            const char *val = xml_attr_val(attr);
            if (!val || !val[0]) { return false; }
            char *eval;

            strtab->count = strtol(val, &eval, 10);
            if (eval[0]) { return false; }

            strtab->base = calloc(strtab->count, sizeof(char *));
            if (!strtab->base) { perror("calloc"); }

            return false;
        } else {
            return true;
        }
    });

    if (!strtab->base)
    {
        fprintf(stderr, "Warning: Excel document does not specify string table size.\n");

        // We have to just visit the tree and count.
        strtab->count = 0;

        xml_visit_tree(table, 1, ^(xmlNodePtr node, size_t depth, size_t n) {
            strtab->count++;
            return false;
        });

        strtab->base = calloc(strtab->count, sizeof(char *));

        if (!strtab->base)
        {
            perror("calloc");

            xmlFreeDoc(strdata->doc);
            zclose(archive);

            return 1;
        }
    }

    xml_visit_tree(table, 1, ^(xmlNodePtr node, size_t depth, size_t n) {
        xmlNodePtr tnode = xml_find(node, "si.t.text");

        if (!tnode)
        {
            fprintf(stderr, "Warning: Excel document string entry %zu is invalid.\n", n);
            return false;
        }

        // This can happen if we go past the end of the preallocated buffer.
        if (!strtab->base) {
            return false;
        }

        if (n >= strtab->count)
        {
            fprintf(stderr, "Error: Excel document has more strings than indicated!\n");

            free(strtab->base);
            strtab->base = NULL;

            return false;
        }

        strtab->base[n] = (char *)tnode->content;
        return false;
    });

    if (!strtab->base)
    {
        xmlFreeDoc(strdata->doc);
        zclose(archive);

        return 1;
    }

    if (DEBUG_XLSX) {
        printf("Info: Read %zu strings from excel document.\n", strtab->count);
    }

    return 0;
}

static int _xlsx_sheet(zip_t *archive, const char *path, struct xlsx *doc)
{
    xmlNodePtr wsdata = _xlsx_xl_root(archive, path);
    if (!wsdata) { return 1; }

    xmlNodePtr sheet = xml_find(wsdata, "worksheet.sheetData");

    if (!sheet)
    {
        fprintf(stderr, "Error: Excel document has no sheet data!\n");

        xmlFreeDoc(wsdata->doc);
        zclose(archive);

        return 1;
    }

    xml_dump_tree(sheet);

    xmlFreeDoc(wsdata->doc);
    return 0;
}

struct xlsx *xlsx_doc_at(const char *path)
{
    // XLSX files are glorified zip archives.
    zip_t *archive = zopen(path);
    if (!archive) { return NULL; }

    // We use the `rels` file to figure out where the data we care about is.
    xmlNodePtr rels = zxml_root_at(archive, XLSX_RELS);

    if (!rels)
    {
        zclose(archive);
        return NULL;
    }

    // Find here really just checks and makes sure the root name is correct.
    xmlNodePtr rdata = xml_find(rels, "Relationships");

    if (!rdata)
    {
        fprintf(stderr, "Error: Excel document is missing relationship info!\n");

        xmlFreeDoc(rels->doc);
        zclose(archive);

        return NULL;
    }

    // We want two things: `worksheet` and `sharedStrings` data.
    // The first specifies how data is laid out, and the second specifies any string content of cells.
    __block char *worksheet = NULL;
    __block char *strings = NULL;

    xml_visit_tree(rdata, 0, ^(xmlNodePtr node, size_t depth, size_t _) {
        if (strcmp("Relationship", (const char *)node->name)) {
            return false;
        }

        // We use the `Type` attribute to determine the referenced object type,
        //   and the `Target` attribute tells us the path inside the archive.
        __block char *target = NULL;
        __block char *type = NULL;

        xml_node_attributes(node, ^(xmlAttrPtr attr, size_t _) {
            if (!strcmp("Type", (const char *)attr->name)) {
                // We just want to look at the final URL component here.
                const char *val = xml_attr_val(attr);
                char *last = strrchr(val, '/');

                type = (last ? &last[1] : "?");
            } else if (!strcmp("Target", (const char *)attr->name)) {
                // This is a path relative to the `xl` directory.
                target = xml_attr_val(attr);
            }

            return ((!target) || (!type));
        });

        if (!target || !type) {
            return false;
        }

        if (DEBUG_XLSX) {
            printf("Excel document has XML document of type '%s' at '%s'.\n", type, target);
        }

        if (!strcmp(type, "worksheet")) {
            worksheet = target;
        } else if (!strcmp(type, "sharedStrings")) {
            strings = target;
        }

        return false;
    });

    if (!worksheet || !strings)
    {
        fprintf(stderr, "Error: Excel document is missing worksheet and/or strings.\n");

        xmlFreeDoc(rels->doc);
        zclose(archive);

        return NULL;
    }

    // We allocate this later to remove unecessary `free`s earlier.
    struct xlsx *doc = malloc(sizeof(struct xlsx));

    if (!doc)
    {
        perror("malloc");

        xmlFreeDoc(rels->doc);
        zclose(archive);

        return NULL;
    }

    // Build strings table. The worksheet will index into here.
    if (_xlsx_strtab(archive, strings, &doc->strtab))
    {
        xmlFreeDoc(rels->doc);
        free(doc);

        return NULL;
    }

    if (_xlsx_sheet(archive, worksheet, doc))
    {
        xmlFreeDoc(rels->doc);

        xmlFreeDoc(doc->strtab.ref);
        free(doc->strtab.base);
        free(doc);

        return NULL;
    }

    xmlFreeDoc(rels->doc);
    zclose(archive);

    return doc;
}

int main(int argc, const char *const *argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Error: Need exactly 1 argument.\n");
        return 1;
    }

    struct xlsx *document = xlsx_doc_at(argv[1]);

    return 0;
}
