// changes/180-wcslwr-s/reference.c
// The correctness oracle: the obvious scalar _wcslwr_s. Not fast; just correct.
// Contract derived in probes/wls.c and fuzz-confirmed against the live export
// (1,000,000 cases, 0 mismatches):
//   * the fold is EXACTLY the 26 ASCII letters A-Z -> a-z (swept over all 65535 code units;
//     0 differences from the plain rule, 947 from RtlDowncaseUnicodeChar);
//   * success -> 0, lowercased in place, nothing past the terminator touched;
//   * no terminator strictly inside numberOfElements -> EINVAL (22) AND str[0] = 0, including
//     when numberOfElements == 0;
//   * VALIDATE FIRST: there is NO partial fold on the error path.
#include <wchar.h>
#include <stddef.h>

int ref_wcslwr_s(wchar_t* str, size_t numberOfElements){
    size_t k = 0;
    while(k < numberOfElements && str[k]) ++k;
    if(k == numberOfElements){          /* no terminator inside the bound (covers 0) */
        str[0] = 0;
        return 22;                      /* EINVAL */
    }
    for(size_t i = 0; i < k; ++i)
        if(str[i] >= L'A' && str[i] <= L'Z') str[i] = (wchar_t)(str[i] + 32);
    return 0;
}
