// Small tool to dump xml at a given path in a zip archive.
#include "xml.h"

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
