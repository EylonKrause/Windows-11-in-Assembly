// changes/155-wcsncpy-s/reference.c
// Oracle for ucrtbase!wcsncpy_s, transcribed from the UCRT algorithm and confirmed against the live
// export -- return code, handler invocation, and the exact bytes left in dst, including both
// truncation paths (ERANGE empties the string; _TRUNCATE terminates the last byte instead).
#include <errno.h>
#include <stddef.h>
#include <wchar.h>

#ifndef STRUNCATE
#define STRUNCATE 80
#endif

int ref_wcsncpy_s(wchar_t* dst, size_t size, const wchar_t* src, size_t count, int* iph)
{
    if (count == 0 && dst == 0 && size == 0) return 0;          /* documented no-op */
    if (dst == 0 || size == 0) { if (iph) ++*iph; return EINVAL; }
    if (count == 0) { *dst = 0; return 0; }                     /* before the src check */
    if (src == 0) { *dst = 0; if (iph) ++*iph; return EINVAL; }

    wchar_t* p   = dst;
    size_t avail = size;

    if (count == (size_t)-1) {                                  /* _TRUNCATE */
        while ((*p++ = *src++) != 0 && --avail > 0) { }
        if (avail == 0) { *--p = 0; return STRUNCATE; }
    } else {
        while (count > 0 && (*p++ = *src++) != 0 && --avail > 0) --count;
        if (count == 0) *p = 0;
    }

    if (avail == 0) { *dst = 0; if (iph) ++*iph; return ERANGE; }
    return 0;
}
