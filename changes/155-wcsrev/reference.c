// changes/155-wcsrev/reference.c
// Oracle for ucrtbase!_wcsrev: reverse in place, return the argument. No validation, no handler.
#include <wchar.h>

wchar_t* ref_wcsrev(wchar_t* s)
{
    wchar_t* e = s;
    while (*e) ++e;
    --e;
    wchar_t* p = s;
    while (p < e) { wchar_t t = *p; *p = *e; *e = t; ++p; --e; }
    return s;
}
