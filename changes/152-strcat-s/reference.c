// changes/152-strcat-s/reference.c
// Oracle for ucrtbase!strcat_s, transcribed from the UCRT algorithm and confirmed against the live
// export, return code, handler invocation, and the exact bytes left in dst (both the "dst not
// terminated" path, which writes only dst[0], and the ERANGE path, which appends `size - L` bytes
// before emptying the string).
#include <errno.h>
#include <stddef.h>

int ref_strcat_s(char* dst, size_t size, const char* src, int* iph)
{
    if (dst == 0 || size == 0) { if (iph) ++*iph; return EINVAL; }

    char*  p     = dst;
    size_t avail = size;
    while (avail > 0 && *p != 0) { ++p; --avail; }
    if (avail == 0) { *dst = 0; if (iph) ++*iph; return EINVAL; }

    if (src == 0)   { *dst = 0; if (iph) ++*iph; return EINVAL; }

    while ((*p++ = *src++) != 0 && --avail > 0) { }
    if (avail == 0) { *dst = 0; if (iph) ++*iph; return ERANGE; }
    return 0;
}
