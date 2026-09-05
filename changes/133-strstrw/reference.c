// changes/133-strstrw/reference.c
// Oracle for shlwapi!StrStrW: ordinal case-sensitive substring search, identical to wcsstr except an
// empty needle returns NULL (verified vs the live export).
#include <wchar.h>
const wchar_t* ref_strstrw(const wchar_t* h, const wchar_t* n){
    if(n[0]==0) return 0;
    for(; *h; ++h){
        const wchar_t *a=h, *b=n;
        while(*b && *a==*b){ ++a; ++b; }
        if(*b==0) return h;
    }
    return 0;
}
