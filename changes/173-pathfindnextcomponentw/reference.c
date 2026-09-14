// changes/173-pathfindnextcomponentw/reference.c
// The correctness oracle: the obvious scalar PathFindNextComponentW. Not fast; just correct.
// Contract derived in probes/pfnc.c and fuzz-confirmed against the live export
// (2,000,000 cases, 0 mismatches):
//   empty string            -> NULL (the only NULL)
//   first '\' found         -> if the next character is ALSO '\', advance exactly ONE more
//                              (never a whole run), then return one past it
//   no '\' anywhere         -> return a pointer to the TERMINATOR, not NULL
//   The separator is exactly U+005C; a forward slash is NOT one (swept over all 65535 units).
#include <wchar.h>

wchar_t* ref_pathfindnextcomponentw(const wchar_t* psz){
    if(!psz[0]) return 0;
    const wchar_t* s = 0;
    for(int i=0; psz[i]; ++i){
        if(psz[i] == L'\\'){ s = psz + i; break; }
    }
    if(s){
        if(s[1] == L'\\') ++s;          /* exactly one extra, not the whole run */
        return (wchar_t*)(s + 1);
    }
    int n = 0; while(psz[n]) ++n;
    return (wchar_t*)(psz + n);         /* the terminator */
}
