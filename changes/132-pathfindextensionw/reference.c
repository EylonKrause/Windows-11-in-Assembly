// changes/132-pathfindextensionw/reference.c
// Oracle for shlwapi!PathFindExtensionW.
//
// CORRECTED 2026-09-15. The rule originally derived here was INCOMPLETE, and this file, the
// implementation and the correctness test were all wrong together on 295513 of 2015539 enumerated
// strings. See impl.asm for the full account. The rule is:
//
//   the last '.' after the last stopper, where a stopper is a backslash **or a space** -- else a
//   pointer to the terminating NUL. '/' and ':' do NOT stop the search, even though
//   PathFindFileNameW treats both as separators.
//
// Verified against the LIVE export with 0 mismatches over 2015539 strings across two alphabets,
// and the narrow sibling PathFindExtensionA agrees with the wide one on every one of them.
#include <wchar.h>
const wchar_t* ref_pathfindextw(const wchar_t* p){
    const wchar_t* end=p; while(*end) end++;
    for(const wchar_t* q=end; q>p; ){
        --q;
        if(*q==L'.') return q;
        if(*q==L'\\' || *q==L' ') break;   /* the space was the missing half of this rule */
    }
    return end;
}
