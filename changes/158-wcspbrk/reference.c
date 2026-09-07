// changes/158-wcspbrk/reference.c
// Oracle for ucrtbase!wcspbrk: first character of `s` that appears in `set`, else NULL.
#include <stddef.h>
#include <wchar.h>

const wchar_t* ref_wcspbrk(const wchar_t* s, const wchar_t* set)
{
    for (; *s; ++s) {
        const wchar_t* q = set;
        while (*q && *q != *s) ++q;
        if (*q) return s;
    }
    return 0;
}
