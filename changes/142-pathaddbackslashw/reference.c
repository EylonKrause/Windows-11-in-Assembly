// changes/142-pathaddbackslashw/reference.c
// Oracle for shlwapi!PathAddBackslashW (semantics probed against the live export).
// The MAX_PATH rule is "does the RESULT, terminator included, fit in 260 characters?" and it is applied
// BEFORE the already-ends-with-backslash shortcut, so a long path that already ends with '\' still
// returns NULL, even though nothing would be written. That asymmetry (>=260 vs >=259) is why the two
// thresholds differ.
#include <wchar.h>
wchar_t* ref_pathaddbackslashw(wchar_t* p){
    long len=0; while(p[len]) len++;
    if(len==0) return p;                       // empty string: nothing appended
    if(p[len-1]==L'\\'){                       // '/' does not count
        if(len>=260) return 0;
        return p+len;
    }
    if(len>=259) return 0;                     // appending needs len + 2 <= 260
    p[len]=L'\\'; p[len+1]=0;
    return p+len+1;
}
