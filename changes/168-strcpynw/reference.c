// changes/168-strcpynw/reference.c
// The correctness oracle: the obvious scalar StrCpyNW. Not fast; just correct.
// Contract derived in probes/scn.c and fuzz-confirmed against the live export
// (2,000,000 cases, 0 mismatches):
//   cchMax <= 0  -> write NOTHING at all, return dst
//   otherwise    -> copy min(cchMax-1, wcslen(src)) chars, then exactly one NUL
//                   (no strncpy-style NUL fill; nothing past the terminator is touched)
//   returns dst always
#include <wchar.h>

wchar_t* ref_strcpynw(wchar_t* dst, const wchar_t* src, int cchMax){
    if(cchMax <= 0) return dst;
    int n = 0;
    while(n < cchMax-1 && src[n]){ dst[n] = src[n]; ++n; }
    dst[n] = 0;
    return dst;
}
