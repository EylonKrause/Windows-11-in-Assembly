// changes/001-wcslen/reference.c
// The correctness oracle: the obvious scalar wcslen. Not fast; just correct.
#include <stddef.h>
#include <wchar.h>

size_t ref_wcslen(const wchar_t* s) {
    size_t n = 0;
    while (s[n] != 0) ++n;
    return n;
}
