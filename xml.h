#ifndef __XML__
#define __XML__

#include <libxml/parser.h>
#include "zip.h"

// Enable debug messages
#define DEBUG_XML 1

// Separator character used in XML paths (xml_find)
#define XML_PATH_SEP '.'

// Max XML tree nesting depth.
#define XML_MAX_DEPTH 1000

// Return root node for XML tree in file at path. Print diagnostics.
extern xmlNodePtr xml_root_at(const char *path); 

// Return root node for XML tree from document in memory. Print diagnostics.
extern xmlNodePtr xml_root_in(const void *buf, size_t len);

// Return root node for XML tree in file at path in zip archive. Print diagnostics.
extern xmlNodePtr zxml_root_at(zip_t *archive, const char *path);

// Visit XML tree. `depth` is the level of the node, `n` is the index of the child. Return `0` to stop.
extern void xml_visit_tree(xmlNodePtr root, size_t depth, int (^blk)(xmlNodePtr node, size_t depth, size_t n));

// Lookup node at "path" (separated by XML_PATH_SEP) relative to a given node.
extern xmlNodePtr xml_find(xmlNodePtr root, const char *path);

// Visit each attribute of a given node. `n` is the index of the attribute. Return `0` to stop.
extern void xml_node_attributes(xmlNodePtr node, int (^blk)(xmlAttrPtr attr, size_t n));

// Return the string value of an attribute.
extern char *xml_attr_val(xmlAttrPtr attr);

// Lookup the values of a named attribute of a node.
extern char *xml_node_attribute(xmlNodePtr node, const char *name);

// Dump node info to stdout, including namespace, name, and attributes.
extern void xml_dump_node(xmlNodePtr node);

// Dump tree info to stdout.
extern void xml_dump_tree(xmlNodePtr root);

#endif /* !defined(__XML__) */
