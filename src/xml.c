/* ********************************************************** */
/* -*- xml.c -*- XML routines on top of libxml2           -*- */
/* ********************************************************** */
/* Tyler Besselman (C) August 2024                            */
/* ********************************************************** */

#include <strings.h>
#include <stdbool.h>

#include <xml.h>

#define foreach(node, v, f, blk)    \
    do {                            \
        typeof(node) v = (node)->f; \
                                    \
        while (v)                   \
        {                           \
            blk;                    \
                                    \
            v = v->next;            \
        }                           \
    } while (0)

// Get root node from document.
static xmlNodePtr _xml_root_for(xmlDocPtr doc)
{
    xmlNodePtr root = xmlDocGetRootElement(doc);

    if (!root)
    {
        fprintf(stderr, "Error: Failed to get root element for document.\n");
        return NULL;
    }

    return root;
}

xmlNodePtr xml_root_at(const char *path)
{
    xmlDocPtr doc = xmlParseFile(path);

    if (!doc)
    {
        fprintf(stderr, "Error: Failed to parse xml file at '%s'.\n", path);
        return NULL;
    }

    return _xml_root_for(doc);
}

xmlNodePtr xml_root_in(const void *buf, size_t len)
{
    xmlDocPtr doc = xmlParseMemory(buf, len);

    if (!doc)
    {
        fprintf(stderr, "Error: Failed to parse xml document from memory.\n");
        return NULL;
    }

    return _xml_root_for(doc);
}

xmlNodePtr zxml_root_at(zip_t *archive, const char *path)
{
    zip_int64_t idx = zip_name_locate(archive, path, ZIP_FL_ENC_UTF_8);

    if (idx < 0)
    {
        fprintf(stderr, "Error: Zip archive missing path '%s'.\n", path);
        return NULL;
    }

    zip_stat_t zstat;

    if (zip_stat_index(archive, idx, 0, &zstat))
    {
        zerror("zip_stat_index", archive);
        return NULL;
    }

    if (!(zstat.valid & ZIP_STAT_SIZE))
    {
        fprintf(stderr, "Error: Cannot determine size of '%s' in zip archive.\n", path);
        return NULL;
    }

    void *buf = malloc(zstat.size);

    if (!buf)
    {
        perror("malloc");
        return NULL;
    }

    zip_file_t *file = zip_fopen_index(archive, idx, 0);

    if (!file)
    {
        zerror("zip_fopen_index", archive);
        free(buf);

        return NULL;
    }

    zip_int64_t read = zip_fread(file, buf, zstat.size);

    if (read < zstat.size)
    {
        if (read < 0) {
            zerror("zip_fread", archive);
        } else {
            fprintf(stderr, "Error: Could not read path '%s' fully from zip archive (s=%llu,r=%lld)\n", path, zstat.size, read);
        }

        if (zip_fclose(file))
        { zerror("zip_fclose", archive); }

        free(buf);
        return NULL;
    }

    if (DEBUG_XML) {
        printf("Read %lld bytes from '%s' in zip archive.\n", read, path);
    }

    if (zip_fclose(file))
    {
        zerror("zip_fclose", archive);
        free(buf);

        return NULL;
    }

    xmlNodePtr root = xml_root_in(buf, zstat.size);
    free(buf);

    return root;
}

void xml_visit_tree(xmlNodePtr root, size_t depth, int (^blk)(xmlNodePtr node, size_t depth, size_t n))
{
    size_t n = 0;

    foreach(root, child, children, {
        if (blk(child, depth + 1, n))
        {
            if (depth + 1 >= XML_MAX_DEPTH) {
                fprintf(stderr, "Error: Reached maximum nesting depth in XML tree!\n");
            } else {
                xml_visit_tree(child, depth + 1, blk);
            }
        }

        n++;
    });
}

static xmlNodePtr _xml_find_internal(xmlNodePtr root, size_t depth, const char *path)
{
    // We don't support empty path components (this catches '.*', '..', '*.' and empty path)
    if (!path) { return NULL; }

    // Get a pointer to the next path separator char.
    const char *next = strchr(path, '.');

    // Compute how long the next path component is.
    size_t len = next ? (next - path) : strlen(path);

    // Get out if this node doesn't match the first path component.
    if (strncmp(path, (const char *)root->name, len)) {
        return NULL;
    }

    if (!next) {
        // If this is the last path component, we're done.
        return root;
    } else if (depth + 1 >= XML_MAX_DEPTH) {
        // Make sure we don't recurse forever.
        fprintf(stderr, "Error: Reached maximum nesting depth in XML tree!\n");
        return NULL;
    } else {
        // Recurse down a level, checking each child to see if it matches the next path component.
        foreach(root, child, children, {
            xmlNodePtr candidate = _xml_find_internal(child, depth + 1, &next[1]);
            if (candidate) { return candidate; }
        });

        // No children nodes match the target. This path doesn't exist in this tree.
        return NULL;
    }
}

xmlNodePtr xml_find(xmlNodePtr root, const char *path)
{ return _xml_find_internal(root, 1, path); }

void xml_node_attributes(xmlNodePtr node, int (^blk)(xmlAttrPtr attr, size_t n))
{
    xmlAttrPtr attr = node->properties;
    size_t n = 0;

    while (attr)
    {
        if (!blk(attr, n)) {
            return;
        }

        attr = attr->next;
        n++;
    }
}

char *xml_attr_val(xmlAttrPtr attr)
{ return (attr->children ? (char *)attr->children->content : NULL); }

char *xml_node_attribute(xmlNodePtr node, const char *name)
{
    __block char *value = NULL;

    xml_node_attributes(node, ^(xmlAttrPtr attr, size_t _) {
        if (strcmp(name, (char *)attr->name)) { return true; }

        value = xml_attr_val(attr);
        return false;
    });

    return value;
}

void xml_dump_node(xmlNodePtr node)
{
    xmlNsPtr namespace = node->ns;

    if (namespace && namespace->prefix) {
        printf("'%s':", namespace->prefix);
    }

    printf("'%s'", node->name);

    xmlAttrPtr attr = node->properties;

    while (attr)
    {
        printf(" (");

        if (attr->ns && attr->ns->prefix) {
            printf("%s:", attr->ns->prefix);
        }

        printf("%s=[", attr->name);
        printf("%s", xml_attr_val(attr));
        printf("])");

        attr = attr->next;
    }
}

void xml_dump_tree(xmlNodePtr root)
{
    printf("- ");
    xml_dump_node(root);
    putchar('\n');

    xml_visit_tree(root, 1, ^(xmlNodePtr node, size_t depth, size_t n) {
        printf("%*s- ", (int)(depth - 1) * 2, "");
        xml_dump_node(node);
        printf(" [%zu]", n);

        if (node->children || !node->content) {
            putchar('\n');
        } else {
            printf(" \"%s\"\n", node->content);
        }

        return true;
    });
}
