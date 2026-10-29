/**********************************************************************************************
*
*   stdio.h - <stdio.h> shim for the Xbox (nxdk) platform
*
*   This directory precedes the pdclib include directory in the include path, so
*   <stdio.h> resolves to this file first. The real pdclib header is pulled in
*   through #include_next and then the MSVC CRT extensions that the bundled
*   third-party libraries expect are added on top of it.
*
*   See msvc_compat.h for the details.
*
**********************************************************************************************/

#ifndef XBOX_COMPAT_STDIO_H
#define XBOX_COMPAT_STDIO_H

#include_next <stdio.h>

#include "msvc_compat.h"

#endif // XBOX_COMPAT_STDIO_H
