// changes/131-strchrw/reference.c
// Oracle for shlwapi!StrChrW: first occurrence of wMatch, else NULL. Note the divergence from C's
// wcschr -- searching for 0 returns NULL rather than a pointer to the terminator (verified vs live).
#include <wchar.h>
const wchar_t* ref_strchrw(const wchar_t* s, wchar_t c){
    if(c==0) return 0;
    for(; *s; ++s) if(*s==c) return s;
    return 0;
}
