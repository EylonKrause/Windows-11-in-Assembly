// changes/153-wcscat-s/reference.c
// Oracle for ucrtbase!wcscat_s -- the UCRT strcat_s algorithm with `size` counted in wchar_t.
#include <errno.h>
#include <stddef.h>
#include <wchar.h>

int ref_wcscat_s(wchar_t* dst, size_t size, const wchar_t* src, int* iph)
{
    if (dst == 0 || size == 0) { if (iph) ++*iph; return EINVAL; }

    wchar_t* p     = dst;
    size_t   avail = size;
    while (avail > 0 && *p != 0) { ++p; --avail; }
    if (avail == 0) { *dst = 0; if (iph) ++*iph; return EINVAL; }

    if (src == 0)   { *dst = 0; if (iph) ++*iph; return EINVAL; }

    while ((*p++ = *src++) != 0 && --avail > 0) { }
    if (avail == 0) { *dst = 0; if (iph) ++*iph; return ERANGE; }
    return 0;
}
