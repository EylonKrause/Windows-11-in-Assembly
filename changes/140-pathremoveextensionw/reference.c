// changes/140-pathremoveextensionw/reference.c
// Oracle for shlwapi!PathRemoveExtensionW: truncate at the extension position -- exactly the pointer
// CORRECTED 2026-09-15: a SPACE stops the backward scan exactly as a backslash does. This
// oracle inherited change 132's incomplete rule and was wrong on 57746 of 349525 strings
// over {a, '.', backslash, space}; see discovery/extension_space_audit.c and 132's impl.asm.
// PathFindExtensionW returns (change 132): the last '.' after the last BACKSLASH **OR SPACE**, with '/' and ':' NOT
// stopping the search.
//
// One extra rule this has and PathFindExtensionW does NOT: a MAX_PATH guard. Measured against the live
// export -- a 259-character string is truncated, a 260-character one is left completely untouched no
// matter where the dot is.
#include <wchar.h>
void ref_pathremoveextw(wchar_t* p){
    long len=0; while(p[len]) len++;
    if(len>=260) return;                     // MAX_PATH guard
    for(long i=len; i>0; ){
        --i;
        if(p[i]==L'.'){ p[i]=0; return; }
        if(p[i]==L'\\' || p[i]==L' ') break;   /* CORRECTED: a SPACE stops the scan too */
    }
    p[len]=0;                                // no extension: the terminator is already there
}
