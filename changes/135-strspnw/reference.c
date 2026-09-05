// changes/135-strspnw/reference.c
// Oracle for shlwapi!StrSpnW: length of the initial run of characters all present in pszSet.
#include <wchar.h>
int ref_strspnw(const wchar_t* s, const wchar_t* set){
    int n=0;
    for(; s[n]; ++n){
        const wchar_t* q=set;
        while(*q && *q!=s[n]) ++q;
        if(*q==0) break;                 // s[n] is not in the set -> span ends
    }
    return n;
}
