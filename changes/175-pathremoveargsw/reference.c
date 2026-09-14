// changes/175-pathremoveargsw/reference.c
// The correctness oracle: the obvious scalar PathRemoveArgsW. Not fast; just correct.
// Contract derived in probes/pra.c + probes/pra2.c and fuzz-confirmed against the live export
// (2,000,000 cases, 0 mismatches). Three behaviours:
//   1. find the first U+0020 OUTSIDE double quotes (each '"' toggles). Exactly U+0020 splits;
//      a tab does not (swept over all 65535 code units).
//   2. if one exists and something follows it: NUL it, and ALSO NUL the LAST space of that run
//      when a non-space follows ("ab   c" writes cells 2 and 4, not 2 and 3).
//   3. if there is NO unquoted space: trim TRAILING blanks, terminating at the first character
//      of the trailing run. This is why '"'+' ' is cut but '"'+' '+'a' is not.
#include <wchar.h>

void ref_pathremoveargsw(wchar_t* psz){
    int n = 0; while(psz[n]) ++n;
    int q = 0, i = -1;
    for(int k=0;k<n;++k){
        if(psz[k] == L' ' && !q){ i = k; break; }
        if(psz[k] == L'"') q ^= 1;
    }
    if(i >= 0){
        int args = i + 1;
        psz[i] = 0;
        if(psz[args] != 0){
            int j = args;
            while(psz[j] == L' ') ++j;
            if(psz[j] != 0) psz[j-1] = 0;
        }
    } else {
        int j = n;
        while(j > 0 && psz[j-1] == L' ') --j;
        if(j < n) psz[j] = 0;
    }
}
