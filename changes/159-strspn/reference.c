// changes/159-strspn/reference.c
// Oracle for ucrtbase!strspn: length of the initial run of characters that all appear in `set`.
#include <stddef.h>

size_t ref_strspn(const char* s, const char* set)
{
    const char* p = s;
    for (; *p; ++p) {
        const char* q = set;
        while (*q && *q != *p) ++q;
        if (!*q) break;                 /* not in the set -> the span ends here */
    }
    return (size_t)(p - s);
}
