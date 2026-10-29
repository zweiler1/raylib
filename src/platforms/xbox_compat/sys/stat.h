/**********************************************************************************************
*
*   xbox_compat/sys/stat.h - Minimal <sys/stat.h> for the Xbox (nxdk) platform
*
*   nxdk and pdclib do not ship a <sys/stat.h>, but raylib (rcore.c) includes it
*   unconditionally and uses stat(), struct stat, st_mode and st_mtime in
*   GetFileModTime() and IsPathFile().
*
*   This header only provides the declarations needed to compile; the actual
*   implementation lives in src/platforms/xbox_compat/xbox_compat.c.
*
**********************************************************************************************/

#ifndef RAYLIB_XBOX_SYS_STAT_H
#define RAYLIB_XBOX_SYS_STAT_H

#include <stdint.h>
#include <time.h>       // time_t

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RAYLIB_XBOX_MODE_T_DEFINED
#define RAYLIB_XBOX_MODE_T_DEFINED
typedef uint32_t mode_t;
#endif

// File type bits (POSIX compatible values)
#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)

struct stat {
    unsigned int st_dev;
    unsigned int st_ino;
    mode_t       st_mode;
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned int st_rdev;
    long         st_size;
    time_t       st_atime;
    time_t       st_mtime;
    time_t       st_ctime;
};

int stat(const char *path, struct stat *buf);

#ifdef __cplusplus
}
#endif

#endif // RAYLIB_XBOX_SYS_STAT_H
