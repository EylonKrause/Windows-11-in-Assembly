// changes/150-strcpy-s/reference.c
// Oracle for ucrtbase!strcpy_s, transcribed from the UCRT algorithm and confirmed against the live
// export (return code, handler invocation, AND the exact bytes left in dst; the ERANGE path writes
// `size` bytes of src before emptying dst, which is observable).
#include <errno.h>
#include <stddef.h>

int ref_strcpy_s(char* dst, size_t size, const char* src, int* iph)
{
    if (dst == 0 || size == 0) { if (iph) ++*iph; return EINVAL; }
    if (src == 0)              { *dst = 0; if (iph) ++*iph; return EINVAL; }

    char*  p     = dst;
    size_t avail = size;
    while ((*p++ = *src++) != 0 && --avail > 0) { }

    if (avail == 0) { *dst = 0; if (iph) ++*iph; return ERANGE; }
    return 0;
}
