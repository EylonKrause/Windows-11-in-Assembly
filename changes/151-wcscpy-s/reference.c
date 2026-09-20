// changes/151-wcscpy-s/reference.c
// Oracle for ucrtbase!wcscpy_s, the same UCRT algorithm as strcpy_s with `size` counted in wchar_t.
#include <errno.h>
#include <stddef.h>
#include <wchar.h>

int ref_wcscpy_s(wchar_t* dst, size_t size, const wchar_t* src, int* iph)
{
    if (dst == 0 || size == 0) { if (iph) ++*iph; return EINVAL; }
    if (src == 0)              { *dst = 0; if (iph) ++*iph; return EINVAL; }

    wchar_t* p     = dst;
    size_t   avail = size;
    while ((*p++ = *src++) != 0 && --avail > 0) { }

    if (avail == 0) { *dst = 0; if (iph) ++*iph; return ERANGE; }
    return 0;
}
