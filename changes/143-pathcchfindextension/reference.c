// changes/143-pathcchfindextension/reference.c
// Oracle for kernelbase!PathCchFindExtension (semantics probed against the live export).
// The extension position is identical to shlwapi!PathFindExtensionW (change 132): the last '.' after
// the last BACKSLASH, with '/' and ':' NOT stopping the search. Around that sit the HRESULT rules.
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
        if(p[i]==L'\\') break;
    }
    *ppext=p+len;
    return 0;
}
