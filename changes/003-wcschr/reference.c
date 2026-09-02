// changes/003-wcschr/reference.c -- correctness oracle.
#include <wchar.h>
wchar_t* ref_wcschr(const wchar_t* s, wchar_t c) {
    for (;;) {
        if (*s == c) return (wchar_t*)s;   // c==0 -> returns terminator
        if (*s == 0) return 0;
        ++s;
    }
}
