// changes/136-strcspnw/reference.c
// Oracle for shlwapi!StrCSpnW: length of the initial run of characters NOT present in pszSet.
#include <wchar.h>
int ref_strcspnw(const wchar_t* s, const wchar_t* set){
    int n=0;
    for(; s[n]; ++n){
        const wchar_t* q=set;
        while(*q && *q!=s[n]) ++q;
        if(*q) break;                    // s[n] IS in the set -> span ends
    }
    return n;
}
