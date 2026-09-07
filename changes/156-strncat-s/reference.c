// changes/156-strncat-s/reference.c
// Oracle for ucrtbase!strncat_s, transcribed from the UCRT algorithm and confirmed against the live
// export -- return code, handler invocation, and the exact bytes left in dst, including the three
// partial-write paths (an unterminated dst writes only dst[0]; ERANGE appends then empties the
// string; _TRUNCATE appends then terminates the last byte of the buffer instead).
#include <errno.h>
#include <stddef.h>

#ifndef STRUNCATE
#define STRUNCATE 80
#endif

int ref_strncat_s(char* dst, size_t size, const char* src, size_t count, int* iph)
{
    if (count == 0 && dst == 0 && size == 0) return 0;          /* documented no-op */
    if (dst == 0 || size == 0) { if (iph) ++*iph; return EINVAL; }
    if (count == 0 && src == 0) return 0;                       /* nothing written at all */
    if (src == 0) { *dst = 0; if (iph) ++*iph; return EINVAL; }

    char*  p     = dst;
    size_t avail = size;
    while (avail > 0 && *p != 0) { ++p; --avail; }
    if (avail == 0) { *dst = 0; if (iph) ++*iph; return EINVAL; }

    if (count == (size_t)-1) {                                  /* _TRUNCATE */
        while ((*p++ = *src++) != 0 && --avail > 0) { }
    } else {
        while (count > 0 && (*p++ = *src++) != 0 && --avail > 0) --count;
        if (count == 0) *p = 0;
    }

    if (avail == 0) {
        if (count == (size_t)-1) { dst[size - 1] = 0; return STRUNCATE; }
        *dst = 0; if (iph) ++*iph; return ERANGE;
    }
    return 0;
}
