// changes/144-pathcchremoveextension/reference.c
// Oracle for kernelbase!PathCchRemoveExtension (semantics probed against the live export).
// CORRECTED 2026-09-15: a SPACE stops the backward scan exactly as a backslash does. This
// oracle inherited change 132's incomplete rule and was wrong on 57746 of 349525 strings
// over {a, '.', backslash, space}; see discovery/extension_space_audit.c and 132's impl.asm.
// Same extension rule as changes 132/143 and the same cch validation as 143, but the return value
// distinguishes work done: S_OK when an extension was removed, S_FALSE when there was none.
#include <wchar.h>
#define WIA_EINVAL 0x80070057L
long ref_pathcchremoveext(wchar_t* p, unsigned long long cch){
    if(cch==0 || cch>32768) return WIA_EINVAL;
    unsigned long long len=0;
    while(len<cch && p[len]) len++;
    if(len==cch) return WIA_EINVAL;                 // not terminated inside cch
    if(len>=260) return WIA_EINVAL;                 // extra length limit (PathCchFindExtension has none)
    for(unsigned long long i=len; i>0; ){
        --i;
        if(p[i]==L'.'){ p[i]=0; return 0; }         // S_OK
        if(p[i]==L'\\' || p[i]==L' ') break;   /* CORRECTED: a SPACE stops the scan too */
    }
    return 1;                                        // S_FALSE: no extension, buffer untouched
}
