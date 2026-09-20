// changes/174-pathundecoratew/reference.c
// The correctness oracle: the obvious scalar PathUndecorateW. Not fast; just correct.
// Contract derived in probes/pud.c and fuzz-confirmed against the live export
// (2,000,000 cases, 0 mismatches). The decoration goes only when ALL of these hold:
//   (a) it is in the LAST component (after the last backslash);
//   (b) its ']' is the character immediately before the extension, the LAST '.' after the
//       last backslash OR SPACE (see the correction note), or
//       immediately before the end of the string when the component has no '.';
//   (c) the contents are decimal digits, possibly NONE ("file[].txt" -> "file.txt");
//   (d) the '[' is not the component's first character ("[1].txt" is left alone).
// The gap is closed by moving the remainder down; the stale tail past the new terminator is
// left untouched, exactly as the shipped function leaves it.
#include <wchar.h>

void ref_pathundecoratew(wchar_t* psz){
    int n = 0; while(psz[n]) ++n;
    int comp = 0;
    for(int i=0;i<n;i++) if(psz[i] == L'\\') comp = i+1;

    /* CORRECTED 2026-09-15: the extension search stops at a SPACE as well as a backslash, which is
       change 132's rule and the half this change shipped without. `comp` below stays backslash-only,
       because conjunct (d) is about the COMPONENT, not the extension search. */
    int ext = n;
    for(int q=n;q>0;){
        --q;
        if(psz[q] == L'.'){ ext = q; break; }
        if(psz[q] == L'\\' || psz[q] == L' ') break;
    }

    if(ext-1 <= comp) return;                 /* no room for a group before the extension */
    if(psz[ext-1] != L']') return;            /* the group must hug the extension */

    int j = ext-2;
    while(j > comp && psz[j] >= L'0' && psz[j] <= L'9') --j;
    if(j <= comp) return;                     /* '[' may not be the component's first char */
    if(psz[j] != L'[') return;

    int k = j, m = ext;                       /* delete [j, ext-1] inclusive */
    while(psz[m]) psz[k++] = psz[m++];
    psz[k] = 0;
}
