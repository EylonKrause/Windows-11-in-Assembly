// changes/185-wcsnset-s/reference.c
// The correctness oracle: the obvious scalar _wcsnset_s. Not fast; just correct.
// Contract derived in ../184-strnset-s/probes/sns.c, which fuzzed the byte AND the wide form
// side by side against the live exports (1,000,000 cases each, 0 mismatches).
//   * numberOfElements == 0     -> EINVAL (22), NOTHING written;
//   * no terminator inside the bound -> fill min(count, numberOfElements-1) cells, then
//     str[0] = 0, return EINVAL (22);
//   * otherwise -> fill min(count, length) cells, keep the rest of the string, return 0.
//   * _TRUNCATE ((size_t)-1) is NOT special-cased: it is simply a very large count.
#include <stddef.h>
#include <wchar.h>

int ref_wcsnset_s(wchar_t* str, size_t numberOfElements, wchar_t c, size_t count){
    if(numberOfElements == 0) return 22;         /* EINVAL, nothing written */
    size_t k = 0;
    while(k < numberOfElements && str[k]) ++k;
    if(k == numberOfElements){                   /* no terminator inside the bound */
        size_t lim = numberOfElements - 1;
        if(count < lim) lim = count;
        for(size_t i = 0; i < lim; ++i) str[i] = c;
        str[0] = 0;
        return 22;
    }
    {
        size_t lim = k;
        if(count < lim) lim = count;
        for(size_t i = 0; i < lim; ++i) str[i] = c;
    }
    return 0;
}
