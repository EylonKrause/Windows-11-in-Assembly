// changes/211-lstrcpyna/reference.c
// The correctness oracle for kernelbase!lstrcpynA. Not fast; just obviously right.
//
// Contract, every line measured against the live NARROW export in probes/lcpa.c:
//   * copies at most n-1 characters, stopping early at the source's NUL, then writes ONE terminator;
//   * the destination is NOT padded. "ab" into n=10 leaves cells 2..9 untouched, which rules out a
//     strncpy-shaped implementation and is the reason the correctness test compares the whole buffer;
//   * n == 0 writes nothing at all -- not even a terminator -- and still returns the destination;
//   * n is used UNSIGNED: -1 and -1000 both copy the whole string rather than meaning "empty";
//   * a NULL source or destination returns NULL;
//   * the copy is byte-wise -- GetACP() is 1252 here with ZERO DBCS lead bytes, so no double-byte
//     character can exist to be split and no DBCS-aware truncation rule is observable;
//   * a faulting source returns NULL with the readable prefix already copied. The oracle cannot
//     reproduce that portably, so the page-guard case is compared against the LIVE export only --
//     see correctness.c, where it is handled separately.
#include <windows.h>

char* ref_lstrcpyna(char* dst, const char* src, int n)
{
    unsigned int max;
    unsigned int i;

    if (dst == 0 || src == 0) return 0;
    if (n == 0) return dst;                    /* nothing written, not even a terminator */

    max = (unsigned int)n - 1u;                /* n is unsigned: -1 becomes 0xFFFFFFFE here */

    for (i = 0; i < max; ++i) {
        char c = src[i];
        if (c == 0) break;
        dst[i] = c;
    }
    dst[i] = 0;
    return dst;
}
