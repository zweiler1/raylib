/**********************************************************************************************
*
*   msvc_compat.h - MSVC CRT extension shims for the Xbox (nxdk) platform
*
*   The Xbox target triple (x86-windows-msvc) defines _MSC_VER, so third-party
*   libraries bundled with raylib (stb_image, tinyobj_loader_c, cgltf, ...) take
*   their MSVC code paths. nxdk/pdclib only implement a subset of the Microsoft
*   CRT, so the missing pieces are declared here and defined in xbox_compat.c.
*
*   This header is force-included (-include msvc_compat.h) for every raylib
*   translation unit of the Xbox backend.
*
**********************************************************************************************/

#ifndef XBOX_MSVC_COMPAT_H
#define XBOX_MSVC_COMPAT_H

#include <stdio.h>      // Required for: FILE

// MSVC macro, normally provided by <stdlib.h>
#ifndef _countof
    #define _countof(array) (sizeof(array)/sizeof((array)[0]))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// MSVC "secure" CRT functions used by the bundled third-party libraries
int fopen_s(FILE **streamptr, const char *filename, const char *mode);
int sscanf_s(const char *s, const char *format, ...);

// MSVC 64-bit stdio file position functions
long long _ftelli64(FILE *stream);
int _fseeki64(FILE *stream, long long offset, int origin);

#ifdef __cplusplus
}
#endif

#endif // XBOX_MSVC_COMPAT_H
