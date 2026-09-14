// changes/178-wcsupr-s/reference.c
// The correctness oracle: the obvious scalar _wcsupr_s. Not fast; just correct.
// Contract derived in probes/wus.c and fuzz-confirmed against the live export
// (1,000,000 cases, 0 mismatches):
//   * the fold is EXACTLY the 26 ASCII letters a-z (swept over all 65535 code units; it is NOT
//     RtlUpcaseUnicodeChar, which differs in 947 cases);
//   * success -> 0, upcased in place, nothing past the terminator touched;
//   * if the string does not terminate strictly inside numberOfElements -> EINVAL (22) AND
//     str[0] is set to 0. That write happens even when numberOfElements == 0.
// The real function also invokes the invalid-parameter handler; the ASM does that through
// ucrtbase's own _invalid_parameter_noinfo (the convention changes 150-157 established).
// This oracle deliberately does not, so that correctness.c can compare buffers and returns
// with the handler suppressed.
#include <wchar.h>
#include <stddef.h>

int ref_wcsupr_s(wchar_t* str, size_t numberOfElements){
    size_t k = 0;
    while(k < numberOfElements && str[k]) ++k;
    if(k == numberOfElements){          /* no terminator inside the bound (covers 0) */
        str[0] = 0;
        return 22;                      /* EINVAL */
    }
    for(size_t i = 0; i < k; ++i)
        if(str[i] >= L'a' && str[i] <= L'z') str[i] = (wchar_t)(str[i] - 32);
    return 0;
}
