// changes/156-wcsspn/reference.c
// Oracle for ucrtbase!wcsspn: length of the initial run of characters that all appear in `set`.
#include <stddef.h>
#include <wchar.h>

size_t ref_wcsspn(const wchar_t* s, const wchar_t* set)
{
    const wchar_t* p = s;
    for (; *p; ++p) {
        const wchar_t* q = set;
        while (*q && *q != *p) ++q;
        if (!*q) break;                 /* not in the set -> the span ends here */
    }
    return (size_t)(p - s);
}
