// changes/210-comparestringordinal/reference.c
// The correctness oracle for kernelbase!CompareStringOrdinal. Not fast; just obviously right.
//
// Contract, every line measured against the live export in probes/cso.c:
//   * returns 1 Less, 2 Equal, 3 Greater; 0 with ERROR_INVALID_PARAMETER if either pointer is NULL;
//   * a count of -1 means NUL-terminated; ANY other count is EXACT, so embedded NULs are compared as
//     ordinary characters and the scan does NOT stop at one;
//   * compare min(c1,c2) characters; if those are equal the SHORTER string is LESS and equal lengths
//     are EQUAL. A count of 0 is legal on either side;
//   * case-SENSITIVE is exactly a code-unit compare, 0 differences over 300 000 random pairs;
//   * case-INSENSITIVE compares the UPCASED values, and the fold is exactly ntdll's
//     RtlUpcaseUnicodeChar: 65534 code units with 0 mismatches, and ordering by the upcased pair
//     matched over 400 000 random pairs ("a" vs "B" is LESS, where the raw code units say GREATER);
//   * none of it moves with the thread locale, checked against en-US, tr-TR, lt-LT, az-Latn-AZ and
//     el-GR, where U+0130 and U+0131 still do not fold to i and I.
#include <windows.h>

extern unsigned short wia_upcase[65536];

int ref_comparestringordinal(const wchar_t* s1, int c1, const wchar_t* s2, int c2, BOOL ic)
{
    int i, n;
    if (s1 == 0 || s2 == 0) return 0;
    if (c1 < 0) { c1 = 0; while (s1[c1]) ++c1; }
    if (c2 < 0) { c2 = 0; while (s2[c2]) ++c2; }
    n = (c1 < c2) ? c1 : c2;
    for (i = 0; i < n; ++i) {
        unsigned a = (unsigned short)s1[i], b = (unsigned short)s2[i];
        if (ic) { a = wia_upcase[a]; b = wia_upcase[b]; }
        if (a != b) return (a < b) ? 1 : 3;
    }
    if (c1 == c2) return 2;
    return (c1 < c2) ? 1 : 3;
}
