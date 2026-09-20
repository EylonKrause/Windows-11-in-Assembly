// changes/182-strset-s/reference.c
// The correctness oracle: the obvious scalar _strset_s. Not fast; just correct.
// Contract derived in probes/sss.c and fuzz-confirmed against the live export
// (1,000,000 cases, 0 mismatches). The FILL family differs from the case-fold family:
//   * numberOfElements == 0 -> EINVAL (22), nothing written;
//   * no terminator inside the bound -> PARTIAL FILL of numberOfElements-1 cells, then
//     str[0] = 0, return EINVAL (22);
//   * otherwise -> fill every cell before the terminator, keep it, return 0.
// (Changes 178-181 validate first and write nothing on error; change 150's strcpy_s leaves a
//  partial copy. Three different behaviours in one CRT, each measured, none inherited.)
#include <stddef.h>

int ref_strset_s(char* str, size_t numberOfElements, int c){
    if(numberOfElements == 0) return 22;         /* EINVAL, nothing written */
    size_t k = 0;
    while(k < numberOfElements && str[k]) ++k;
    if(k == numberOfElements){                   /* no terminator inside the bound */
        for(size_t i = 0; i + 1 < numberOfElements; ++i) str[i] = (char)c;
        str[0] = 0;
        return 22;
    }
    for(size_t i = 0; i < k; ++i) str[i] = (char)c;
    return 0;
}
