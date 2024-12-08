#include <strings.h>
#include <stdbool.h>
#include <stdlib.h>

#include "xlsx.h"
#include "xml.h"

// "rels" file stores some info on how to access worksheet data.
#define XLSX_RELS "xl/_rels/workbook.xml.rels"

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
        if (!strcmp("count", (char *)attr->name)) {
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

// Process the main `sheet` data for this document. Here, we read in the values.
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

    doc->rows = 0;
    doc->cols = 0;

    // Count rows and columns to just do a single big allocation.
    xml_visit_tree(sheet, 1, ^(xmlNodePtr row, size_t _, size_t i) {
        if (i > doc->rows) { doc->rows = i; }

        // Realistically, it seems columns are maximal on the first row and only decrease
        //   or stay the same on subsequent rows, but this is safer.
        xml_visit_tree(row, 2, ^(xmlNodePtr col, size_t _, size_t j) {
            if (j > doc->cols) { doc->cols = j; }

            return false;
        });

        return false;
    });

    if (DEBUG_XLSX) {
        printf("Document has %zu rows, %zu cols (mem=%zu).\n", doc->rows, doc->cols, doc->rows * doc->cols * sizeof(struct xlsx_value));
    }

    // Do one big allocation.
    doc->grid = malloc(doc->rows * doc->cols * sizeof(struct xlsx_value));

    if (!doc->grid)
    {
        perror("malloc");

        xmlFreeDoc(wsdata->doc);
        zclose(archive);

        return 1;
    }

    // Second time visiting the full document.
    // We could wrap this together with some dynamic allocation, but I like this way better.
    // Even on documents that are many megabytes, this is pretty quick.
    xml_visit_tree(sheet, 1, ^(xmlNodePtr row, size_t depth, size_t i) {
        // This can happen on failure in the inner loop.
        if (!doc->grid) { return false; }

        // Visit each column, parsing grid values as we go.
        xml_visit_tree(row, 2, ^(xmlNodePtr col, size_t depth, size_t j) {
            // I store with striped rows, columns in order within rows.
            size_t idx = (doc->cols * i) + j;

            struct xlsx_value *slot = &doc->grid[idx];
            slot->type = XLSX_TYPE_NULL;

            // The node which actually holds the value of this cell.
            xmlNodePtr val = xml_find(col, "c.v.text");

            // No value.
            if (!val || !val->content || !val->content[0]) {
                return false;
            }

            // For strings, we need to determine type by attribute.
            xml_node_attributes(col, ^(xmlAttrPtr attr, size_t _) {
                if (strcmp("t", (char *)attr->name)) { return true; }
                const char *type = xml_attr_val(attr);

                // String table indicies are "s". Literal strings are "str"
                if (!strcmp("s", type)) {
                    slot->type = XLSX_TYPE_STR;
                } else if (!strcmp("str", type)) {
                    slot->type = XLSX_TYPE_LSTR;
                } else {
                    fprintf(stderr, "Warning: Excel document specifies unknown type '%s' at (%zu, %zu)\n", type, j, i);
                    slot->type = XLSX_TYPE_LSTR; // We can always just copy the value as a string.
                }

                return false;
            });

            // This value has meaning now, and we may know its type.
            const char *value = (char *)val->content;
            char *end; // Used for string conversions below.

            // Check if something bad happens and get out.
            #define _give_up(thing)                                                     \
                do {                                                                    \
                    fprintf(stderr, "Error: Excel document has malformed" thing "!\n"); \
                                                                                        \
                    /* Unwind what we've done. Free any dup'd strings */                \
                    for (int64_t k = (idx - 1); k >= 0; k--) {                          \
                        if (doc->grid[idx].type == XLSX_TYPE_LSTR) {                    \
                            free(doc->grid[idx].str);                                   \
                        }                                                               \
                    }                                                                   \
                                                                                        \
                    /* Free the data grid to indicate failure. */                       \
                    free(doc->grid);                                                    \
                                                                                        \
                    return false;                                                       \
                } while (0)

            #define _check_conv(thing)  if (end[0]) { _give_up(thing); }

            if (slot->type == XLSX_TYPE_STR) {
                // This is a string table offset.
                size_t idx = strtoll(value, &end, 10);
                _check_conv("string table index");

                slot->sref = idx;
            } else if (slot->type == XLSX_TYPE_LSTR) {
                // Unlike the string table, I opt to dup the value here.
                // These are much less dense in the sheet document vs the string table.
                slot->str = strdup(value);
            } else {
                // Determine float vs int by the presence of a dot.
                char *dot = strchr((char *)val->content, '.');

                if (dot) {
                    slot->type = XLSX_TYPE_FLOAT;
                    slot->fval = strtod(value, &end);

                    _check_conv("float value");
                } else {
                    slot->type = XLSX_TYPE_INT;
                    slot->ival = strtoll(value, &end, 10);

                    _check_conv("integer value");
                }
            }

            return false;
        });

        return false;
    });

    // We're done with this in either case.
    xmlFreeDoc(wsdata->doc);

    // Check if we failed.
    if (!doc->grid)
    {
        zclose(archive);
        return 1;
    }

    if (DEBUG_XLSX) {
        printf("Finished reading %zu values.\n", doc->rows * doc->cols);
    }

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
        if (strcmp("Relationship", (char *)node->name)) {
            return false;
        }

        // We use the `Type` attribute to determine the referenced object type,
        //   and the `Target` attribute tells us the path inside the archive.
        __block char *target = NULL;
        __block char *type = NULL;

        xml_node_attributes(node, ^(xmlAttrPtr attr, size_t _) {
            if (!strcmp("Type", (char *)attr->name)) {
                // We just want to look at the final URL component here.
                const char *val = xml_attr_val(attr);
                char *last = strrchr(val, '/');

                type = (last ? &last[1] : "?");
            } else if (!strcmp("Target", (char *)attr->name)) {
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

void xlsx_foreach_row(struct xlsx *doc, int (^blk)(struct xlsx_value *row, size_t n))
{
    for (size_t i = 0; i < xlsx_rows(doc); i++)
    {
        struct xlsx_value *row = &doc->grid[i * xlsx_cols(doc)];
        if (!blk(row, i)) { break; }
    }
}

// This could be more efficient, but it would take a lot of extra memory.
void xlsx_iter_col(struct xlsx *doc, size_t col, int (^blk)(struct xlsx_value *entry, size_t n))
{
    xlsx_foreach_row(doc, ^(struct xlsx_value *row, size_t n) {
        return blk(&row[col], n);
    });
}

void xlsx_doc_free(struct xlsx *doc)
{
    // Clean up any strings we own the memory for
    for (size_t i = 0; i < (doc->rows * doc->cols); i++)
    {
        if (doc->grid[i].type == XLSX_TYPE_LSTR) {
            free(doc->grid[i].str);
        }
    }

    // Clean up this doc we hold pointers to.
    xmlFreeDoc(doc->strtab.ref);

    // Destroy our internal memory
    free(doc->strtab.base);
    free(doc->grid);

    // And finally the structure itself.
    free(doc);
}

int main(int argc, const char *const *argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Error: Need exactly 1 argument.\n");
        return 1;
    }

    struct xlsx *document = xlsx_doc_at(argv[1]);
    xlsx_doc_free(document);

    return 0;
}
