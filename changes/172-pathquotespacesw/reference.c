// changes/172-pathquotespacesw/reference.c
// The correctness oracle: the obvious scalar PathQuoteSpacesW. Not fast; just correct.
// Contract derived in probes/pqs.c and fuzz-confirmed against the live export
// (1,000,000 cases, 0 mismatches):
//   hasSpace = any psz[i] == U+0020 (exactly U+0020; one of 65535 code units; tab does NOT
//              count, and neither does any other Unicode whitespace)
//   hasSpace && n <= 257 -> shift up one char, quote at [0] and [n+1], NUL at [n+2], TRUE
//   otherwise            -> buffer untouched, FALSE
//   An already-quoted path is quoted AGAIN; there is no special case for it.
#include <wchar.h>

int ref_pathquotespacesw(wchar_t* psz){
    int n = 0; while(psz[n]) ++n;
    int hasspace = 0;
    for(int i=0;i<n;i++) if(psz[i] == 0x0020){ hasspace = 1; break; }
    if(!hasspace || n > 257) return 0;
    for(int i=n;i>=0;--i) psz[i+1] = psz[i];      /* backwards: the regions overlap */
    psz[0]     = L'"';
    psz[n+1]   = L'"';
    psz[n+2]   = 0;
    return 1;
}
