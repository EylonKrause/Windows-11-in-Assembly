// changes/170-strcatbuffw/reference.c
// The correctness oracle: the obvious scalar StrCatBuffW. Not fast; just correct.
// Contract derived in probes/scb.c and fuzz-confirmed against the live export
// (2,000,000 cases, 0 mismatches):
//   n = wcslen(dst) (unbounded); room = cchDestBuffSize - n
//   room > 0  -> copy min(room-1, wcslen(src)) chars at dst+n, then exactly one NUL
//   room <= 0 -> dst left completely untouched
//   returns dst always.  cchDestBuffSize is signed.
#include <wchar.h>

wchar_t* ref_strcatbuffw(wchar_t* dst, const wchar_t* src, int cchDestBuffSize){
    int n = 0; while(dst[n]) ++n;
    int room = cchDestBuffSize - n;
    if(room > 0){
        int k = 0;
        while(k < room-1 && src[k]){ dst[n+k] = src[k]; ++k; }
        dst[n+k] = 0;
    }
    return dst;
}
