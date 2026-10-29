/**********************************************************************************************
*
*   xbox_compat.c - MSVC CRT / POSIX shims for the Xbox (nxdk) platform
*
*   nxdk and pdclib do not provide <sys/stat.h>, <io.h> or the full Microsoft
*   CRT surface. The unmodified raylib sources depend on a small part of it
*   (see the headers in this directory). This translation unit provides the
*   corresponding definitions so that a libraylib.a linked into an XBE
*   resolves them.
*
*   NOTE: _findfirst()/_findnext()/_findclose() are currently stubs, so
*   directory enumeration (DirectoryExists(), LoadDirectoryFiles(), ...) reports
*   "no entries".
*
**********************************************************************************************/

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <io.h>
#include <sys/stat.h>

#include "msvc_compat.h"

//----------------------------------------------------------------------------------
// Floating point string conversions
//----------------------------------------------------------------------------------

// NOTE: pdclib declares atof()/strtof()/strtod()/strtold() but does not
// implement them (see the TODO in its <stdlib.h>), and the Xbox backend cannot
// use the MSVC CRT. raylib's bundled third-party code (par_shapes, cgltf) calls
// atof(), so a small implementation lives here.
//
// strtod()/strtof()/strtold() are NOT defined here on purpose: nxdk's xboxrt
// already defines them as assert(0) stubs, and redefining them would be a
// duplicate symbol. Providing real versions belongs in the nxdk layer.
//
// The conversion accumulates up to 19 significant decimal digits (enough to
// round a double correctly) and then scales by a power of ten. Exponents beyond
// the lookup table are applied by repeated multiplication/division instead of
// pow(), which is not available on this target.

// 10^(2^n) for n = 0..8
static const double xbox_pow10[] = {
    1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256
};

#define XBOX_POW10_COUNT (int)(sizeof(xbox_pow10)/sizeof(xbox_pow10[0]))

static int xbox_isspace(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\v') || (c == '\f') || (c == '\r');
}

static int xbox_isdigit(char c)
{
    return (c >= '0') && (c <= '9');
}

static char xbox_tolower(char c)
{
    return ((c >= 'A') && (c <= 'Z'))? (char)(c - 'A' + 'a') : c;
}

static double xbox_strtod(const char *nptr, char **endptr)
{
    const char *s = nptr;
    int negative = 0;
    int anyDigits = 0;
    int exponent = 0;
    double value = 0.0;

    if (nptr == NULL)
    {
        if (endptr != NULL) *endptr = NULL;
        return 0.0;
    }

    while (xbox_isspace(*s)) s++;

    if ((*s == '+') || (*s == '-'))
    {
        negative = (*s == '-');
        s++;
    }

    // "inf"/"infinity" and "nan" (case insensitive)
    {
        const char *p = s;
        char c0 = xbox_tolower(p[0]);
        char c1 = xbox_tolower(p[1]);
        char c2 = xbox_tolower(p[2]);

        if ((c0 == 'i') && (c1 == 'n') && (c2 == 'f'))
        {
            s += 3;
            if ((xbox_tolower(s[0]) == 'i') && (xbox_tolower(s[1]) == 'n') &&
                (xbox_tolower(s[2]) == 'i') && (xbox_tolower(s[3]) == 't') &&
                (xbox_tolower(s[4]) == 'y')) s += 5;
            if (endptr != NULL) *endptr = (char *)s;
            return negative? -__builtin_inf() : __builtin_inf();
        }

        if ((c0 == 'n') && (c1 == 'a') && (c2 == 'n'))
        {
            s += 3;
            if (endptr != NULL) *endptr = (char *)s;
            return __builtin_nan("");
        }
    }

    // Integer part
    while (xbox_isdigit(*s))
    {
        anyDigits = 1;
        if (value < 1e18) value = value*10.0 + (double)(*s - '0');
        else exponent++;
        s++;
    }

    // Fractional part
    if (*s == '.')
    {
        s++;
        while (xbox_isdigit(*s))
        {
            anyDigits = 1;
            if (value < 1e18)
            {
                value = value*10.0 + (double)(*s - '0');
                exponent--;
            }
            s++;
        }
    }

    if (!anyDigits)
    {
        // No conversion performed: report the original position, like the CRT
        if (endptr != NULL) *endptr = (char *)nptr;
        return 0.0;
    }

    // Exponent part
    if ((*s == 'e') || (*s == 'E'))
    {
        const char *expStart = s;
        int expNegative = 0;
        int expValue = 0;

        s++;

        if ((*s == '+') || (*s == '-'))
        {
            expNegative = (*s == '-');
            s++;
        }

        if (xbox_isdigit(*s))
        {
            while (xbox_isdigit(*s))
            {
                if (expValue < 100000) expValue = expValue*10 + (*s - '0');
                s++;
            }

            exponent += expNegative? -expValue : expValue;
        }
        else s = expStart;     // Not a valid exponent, leave it unconsumed
    }

    // Scale by the decimal exponent
    if (exponent != 0)
    {
        int rem = (exponent < 0)? -exponent : exponent;

        for (int i = XBOX_POW10_COUNT - 1; i >= 0; i--)
        {
            if (rem >= (1 << i))
            {
                if (exponent > 0) value *= xbox_pow10[i];
                else value /= xbox_pow10[i];
                rem -= (1 << i);
            }
        }
    }

    if (endptr != NULL) *endptr = (char *)s;

    return negative? -value : value;
}

double atof(const char *nptr)
{
    return xbox_strtod(nptr, NULL);
}

//----------------------------------------------------------------------------------
// MSVC CRT extensions (declared in msvc_compat.h)
//----------------------------------------------------------------------------------

// NOTE: nxdk/pdclib only implement the Annex K stdio interface as declarations,
// so fopen_s() is a thin wrapper here. Like the MSVC original it returns 0 on
// success and an errno value otherwise.
int fopen_s(FILE **streamptr, const char *filename, const char *mode)
{
    if ((streamptr == NULL) || (filename == NULL) || (mode == NULL)) return EINVAL;

    *streamptr = fopen(filename, mode);

    return (*streamptr == NULL)? ENOENT : 0;
}

// NOTE: The MSVC version enforces the extra buffer-size arguments of the "%s",
// "%c" and "%[" conversions. There is no bounds-checking implementation in
// nxdk, so those arguments are accepted and ignored here.
int sscanf_s(const char *s, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vsscanf(s, format, args);
    va_end(args);

    return result;
}

// NOTE: pdclib keeps 32-bit long values on this 32-bit target, so 64-bit file
// positions can only be forwarded while they fit into fseek()'s long argument.
long long _ftelli64(FILE *stream)
{
    return (long long)ftell(stream);
}

int _fseeki64(FILE *stream, long long offset, int origin)
{
    if ((offset > LONG_MAX) || (offset < LONG_MIN)) return -1;

    return fseek(stream, (long)offset, origin);
}

//----------------------------------------------------------------------------------
// <io.h> implementation
//----------------------------------------------------------------------------------

// NOTE: Only supports existence checks; permissions are meaningless on Xbox.
int _access(const char *path, int mode)
{
    (void)mode;

    FILE *file = fopen(path, "rb");
    if (file == NULL) return -1;

    fclose(file);
    return 0;
}

// TODO: implement on top of NtQueryDirectoryFile() so that directory listing works.
intptr_t _findfirst(const char *filespec, struct _finddata_t *fileinfo)
{
    (void)filespec;

    if (fileinfo != NULL) memset(fileinfo, 0, sizeof(*fileinfo));

    return -1;
}

int _findnext(intptr_t handle, struct _finddata_t *fileinfo)
{
    (void)handle;

    if (fileinfo != NULL) memset(fileinfo, 0, sizeof(*fileinfo));

    return -1;
}

int _findclose(intptr_t handle)
{
    (void)handle;
    return -1;
}

//----------------------------------------------------------------------------------
// <sys/stat.h> implementation
//----------------------------------------------------------------------------------

// NOTE: Size and "is regular file" are derived from a plain fopen(); the
// timestamps are not available through pdclib and are left at zero.
int stat(const char *path, struct stat *buf)
{
    if (buf == NULL) return -1;

    memset(buf, 0, sizeof(*buf));

    FILE *file = fopen(path, "rb");
    if (file == NULL) return -1;

    if (fseek(file, 0, SEEK_END) == 0)
    {
        long size = ftell(file);
        if (size >= 0) buf->st_size = size;
    }

    fclose(file);

    buf->st_mode = S_IFREG;

    return 0;
}
