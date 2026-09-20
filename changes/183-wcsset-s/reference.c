// changes/183-wcsset-s/reference.c
// The correctness oracle: the obvious scalar _wcsset_s. Not fast; just correct.
// Contract derived in ../182-strset-s/probes/sss.c, which fuzzed the byte AND the wide form
// side by side against the live exports (1,000,000 cases each, 0 mismatches). The two are the
// same shape, but that was measured rather than assumed -- the byte/wide pairs in this CRT are
// not always identical (see change 050 vs 048 for a counter-example in the fold set).
//   * numberOfElements == 0 -> EINVAL (22), nothing written;
//   * no terminator inside the bound -> PARTIAL FILL of numberOfElements-1 cells, then
//     str[0] = 0, return EINVAL (22);
//   * otherwise -> fill every cell before the terminator, keep it, return 0.
#include <stddef.h>
#include <wchar.h>

int ref_wcsset_s(wchar_t* str, size_t numberOfElements, wchar_t c){
    if(numberOfElements == 0) return 22;         /* EINVAL, nothing written */
    size_t k = 0;
    while(k < numberOfElements && str[k]) ++k;
    if(k == numberOfElements){                   /* no terminator inside the bound */
        for(size_t i = 0; i + 1 < numberOfElements; ++i) str[i] = c;
        str[0] = 0;
        return 22;
    }
    for(size_t i = 0; i < k; ++i) str[i] = c;
    return 0;
}
