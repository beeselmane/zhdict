// Extra helper routines on top of libzip.
#ifndef __XZIP__
#define __XZIP__ 1

#include <stdio.h>
#include <zip.h>

// Report zip file error with given code.
static inline void _zerror(const char *func, int code)
{
    zip_error_t error; zip_error_init_with_code(&error, code);
    fprintf(stderr, "%s: %s\n", func, zip_error_strerror(&error));
    zip_error_fini(&error);
}

// Report zip file error with code in archive.
static inline void zerror(const char *func, zip_t *archive)
{ fprintf(stderr, "%s: %s\n", func, zip_error_strerror(zip_get_error(archive))); }

// Open zip archive, reporting any errors (NULL on failure)
static inline zip_t *zopen(const char *path)
{
    zip_t *archive;
    int error;

    if (!(archive = zip_open(path, ZIP_RDONLY, &error)))
    {
        _zerror("zip_open", error);
        return NULL;
    }

    return archive;
}

// Close zip archive, reporting any errors.
static inline void zclose(zip_t *archive)
{
    if (zip_close(archive))
    { zerror("zip_close", archive); }
}

#endif /* !defined(__XZIP__) */
