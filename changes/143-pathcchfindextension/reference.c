// changes/143-pathcchfindextension/reference.c
// Oracle for kernelbase!PathCchFindExtension (semantics probed against the live export).
// CORRECTED 2026-09-15: a SPACE stops the backward scan exactly as a backslash does. This
// oracle inherited change 132's incomplete rule and was wrong on 57746 of 349525 strings
// over {a, '.', backslash, space}; see discovery/extension_space_audit.c and 132's impl.asm.
// The extension position is identical to shlwapi!PathFindExtensionW (change 132): the last '.' after
// the last BACKSLASH **OR SPACE**, with '/' and ':' NOT stopping the search. Around that sit the HRESULT rules.
#include <wchar.h>
#define WIA_EINVAL 0x80070057L
long ref_pathcchfindext(const wchar_t* p, unsigned long long cch, const wchar_t** ppext){
    if(cch==0 || cch>32768){ *ppext=0; return WIA_EINVAL; }
    unsigned long long len=0;
    while(len<cch && p[len]) len++;
    if(len==cch){ *ppext=0; return WIA_EINVAL; }   // not terminated inside cch
    for(unsigned long long i=len; i>0; ){
        --i;
        if(p[i]==L'.'){ *ppext=p+i; return 0; }
        if(p[i]==L'\\' || p[i]==L' ') break;   /* CORRECTED: a SPACE stops the scan too */
    }
    *ppext=p+len;
    return 0;
}
