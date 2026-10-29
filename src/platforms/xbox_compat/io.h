/**********************************************************************************************
*
*   xbox_compat/io.h - Minimal <io.h> for the Xbox (nxdk) platform
*
*   nxdk/pdclib does not ship an <io.h>. raylib (rcore.c) uses _access() for
*   FileExists(), and raylib's bundled external/dirent.h uses the _findfirst()
*   family to implement opendir()/readdir().
*
*   The values/structures here match the MSVC CRT so that the unmodified raylib
*   sources compile as-is. The implementations live in
*   src/platforms/xbox_compat/xbox_compat.c.
*
**********************************************************************************************/

#ifndef RAYLIB_XBOX_IO_H
#define RAYLIB_XBOX_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// https://learn.microsoft.com/en-us/cpp/c-runtime-library/access-constants
#ifndef F_OK
#define F_OK 0
#endif

// https://learn.microsoft.com/en-us/cpp/c-runtime-library/finddata-structures
#ifndef RAYLIB_XBOX_FINDDATA_T_DEFINED
#define RAYLIB_XBOX_FINDDATA_T_DEFINED
struct _finddata_t {
    unsigned int  attrib;
    long          time_create;
    long          time_access;
    long          time_write;
    unsigned long size;
    char          name[260];
};
#endif

intptr_t _findfirst(const char *filespec, struct _finddata_t *fileinfo);
int      _findnext(intptr_t handle, struct _finddata_t *fileinfo);
int      _findclose(intptr_t handle);

int _access(const char *path, int mode);

#ifdef __cplusplus
}
#endif

#endif // RAYLIB_XBOX_IO_H
