// changes/138-pathisfilespecw/reference.c
// Oracle for shlwapi!PathIsFileSpecW: FALSE iff the string contains ':' or '\' anywhere.
// Note '/' does NOT disqualify (probed against the live export).
#include <wchar.h>
int ref_pathisfilespecw(const wchar_t* p){
    for(; *p; ++p) if(*p==L':' || *p==L'\\') return 0;
    return 1;
}
