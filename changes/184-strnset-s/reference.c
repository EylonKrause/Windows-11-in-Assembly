// changes/184-strnset-s/reference.c
// The correctness oracle: the obvious scalar _strnset_s. Not fast; just correct.
// Contract derived in probes/sns.c and fuzz-confirmed against the live export (1,000,000 cases,
// 0 mismatches; the wide form in the same run, also 0).
//   * numberOfElements == 0     -> EINVAL (22), nothing written;
//   * no terminator inside the bound -> fill min(count, numberOfElements-1) cells, then
//     str[0] = 0, return EINVAL (22);
//   * otherwise -> fill min(count, length) cells, keep the rest of the string, return 0.
//   * _TRUNCATE ((size_t)-1) is NOT special-cased: it is simply a very large count.
#include <stddef.h>

int ref_strnset_s(char* str, size_t numberOfElements, int c, size_t count){
    if(numberOfElements == 0) return 22;         /* EINVAL, nothing written */
    size_t k = 0;
    while(k < numberOfElements && str[k]) ++k;
    if(k == numberOfElements){                   /* no terminator inside the bound */
        size_t lim = numberOfElements - 1;
        if(count < lim) lim = count;
        for(size_t i = 0; i < lim; ++i) str[i] = (char)c;
        str[0] = 0;
        return 22;
    }
    {
        size_t lim = k;
        if(count < lim) lim = count;
        for(size_t i = 0; i < lim; ++i) str[i] = (char)c;
    }
    return 0;
}
