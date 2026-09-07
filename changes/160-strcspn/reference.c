// changes/160-strcspn/reference.c
// Oracle for ucrtbase!strcspn: length of the initial run of characters in NEITHER `set` nor {NUL}.
#include <stddef.h>

size_t ref_strcspn(const char* s, const char* set)
{
    const char* p = s;
    for (; *p; ++p) {
        const char* q = set;
        while (*q && *q != *p) ++q;
        if (*q) break;                  /* in the set -> the complement span ends here */
    }
    return (size_t)(p - s);
}
