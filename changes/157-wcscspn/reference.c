// changes/157-wcscspn/reference.c
// Oracle for ucrtbase!wcscspn: length of the initial run of characters in NEITHER `set` nor {NUL}.
#include <stddef.h>
#include <wchar.h>

size_t ref_wcscspn(const wchar_t* s, const wchar_t* set)
{
    const wchar_t* p = s;
    for (; *p; ++p) {
        const wchar_t* q = set;
        while (*q && *q != *p) ++q;
        if (*q) break;                  /* in the set -> the complement span ends here */
    }
    return (size_t)(p - s);
}
