// changes/137-strpbrkw/reference.c
// Oracle for shlwapi!StrPBrkW: first character of psz that appears in pszSet, else NULL.
#include <wchar.h>
const wchar_t* ref_strpbrkw(const wchar_t* s, const wchar_t* set){
    for(; *s; ++s){
        for(const wchar_t* q=set; *q; ++q) if(*q==*s) return s;
    }
    return 0;
}
