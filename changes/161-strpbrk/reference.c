// changes/161-strpbrk/reference.c
// Oracle for ucrtbase!strpbrk: first character of `s` that appears in `set`, else NULL.
#include <stddef.h>

const char* ref_strpbrk(const char* s, const char* set)
{
    for (; *s; ++s) {
        const char* q = set;
        while (*q && *q != *s) ++q;
        if (*q) return s;
    }
    return 0;
}
