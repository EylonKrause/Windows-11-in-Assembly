// changes/169-strchrnw/reference.c
// The correctness oracle: the obvious scalar StrChrNW. Not fast; just correct.
// Contract derived in probes/scnw.c and fuzz-confirmed against the live export
// (3,000,000 cases, 0 mismatches):
//   scan i in [0, cchMax): the NUL test comes FIRST, so the terminator stops the search and
//   can never itself match -- wMatch == 0 therefore always returns NULL.
//   Ordinal / case-sensitive. cchMax is unsigned. First match wins.
#include <wchar.h>

wchar_t* ref_strchrnw(const wchar_t* s, wchar_t m, unsigned int cchMax){
    for(unsigned int i = 0; i < cchMax; ++i){
        if(s[i] == 0) return 0;          /* terminator stops the scan, never matches */
        if(s[i] == m) return (wchar_t*)(s + i);
    }
    return 0;
}
