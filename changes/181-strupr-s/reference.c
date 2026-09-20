// changes/181-strupr-s/reference.c
// The correctness oracle: the obvious scalar _strupr_s. Not fast; just correct.
// Contract derived in probes/sus.c and fuzz-confirmed against the live export
// (1,000,000 cases, 0 mismatches):
//   * the fold is exactly the 26 ASCII letters a-z (swept over all 255 byte values, 0
//     differences from the plain rule); bytes >= 0x80 never fold;
//   * success -> 0, uppercased in place, nothing past the terminator touched;
//   * no terminator strictly inside numberOfElements -> EINVAL (22) AND str[0] = 0, including
//     when numberOfElements == 0;
//   * Validate first: there is no partial fold on the error path.
// The real function also invokes the invalid-parameter handler; the ASM does that through
// ucrtbase's own _invalid_parameter_noinfo. This oracle deliberately does not, so correctness.c
// can compare buffers and returns directly.
#include <stddef.h>

int ref_strupr_s(char* str, size_t numberOfElements){
    size_t k = 0;
    while(k < numberOfElements && str[k]) ++k;
    if(k == numberOfElements){          /* no terminator inside the bound (covers 0) */
        str[0] = 0;
        return 22;                      /* EINVAL */
    }
    for(size_t i = 0; i < k; ++i){
        unsigned char c = (unsigned char)str[i];
        if(c >= 'a' && c <= 'z') str[i] = (char)(c - 32);
    }
    return 0;
}
