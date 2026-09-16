/* changes/263-rtlcompareunicodestrings/reference.c
 *
 * THE INDEPENDENT ORACLE for RtlCompareUnicodeStrings.
 *
 * It shares nothing with impl.asm but the contract. impl.asm compares sixteen characters at a time
 * with VPCMPEQW, folds a disagreeing block in-vector when every character in it is ASCII, and only
 * reaches for the table otherwise. This looks at one character, then the next.
 *
 * THE RULES, as probes/contract.c measured them:
 *
 *   * THE RETURN IS THE DIFFERENCE, NOT A SIGN. `A` against `Z` is -25, U+FFFF against U+0000 is
 *     65535, and U+0000 against U+FFFF is -65535. An implementation returning -1/0/1 would satisfy
 *     every caller that writes `< 0` and none that stores the result.
 *   * THE FIRST DIFFERING CHARACTER DECIDES, and the difference is of the two characters ZERO
 *     EXTENDED from sixteen bits.
 *   * WHEN THE COMMON PREFIX IS EQUAL, THE ANSWER IS len1 - len2, IN CHARACTERS: "abc" against
 *     "abcdef" is -3, not -1 and not -6. A difference inside the common part still wins over the
 *     lengths -- "abz" against "abcd" is 23.
 *   * THE LENGTHS ARE IN CHARACTERS. The first run of the discovery probe passed 2 for a single
 *     character and every character came back different from itself.
 *   * CASE-INSENSITIVE RETURNS THE UPCASED DIFFERENCE: `a` against `B` is -1, which is A - B. It is
 *     not the raw difference of 31, so the fold happens BEFORE the subtraction and not merely as an
 *     equality test.
 *   * A NULL POINTER WITH LENGTH ZERO IS NEVER READ.
 *
 * The fold is RtlUpcaseUnicodeChar exactly -- discovery/rtl_cmpstrings_probe.c enumerated a dense
 * sweep of character pairs and found no pair the table disagreed about in either direction -- so the
 * table is built from the OS once, which is what change 210 does for the same reason: a table
 * transcribed into the repository would be a second copy of Windows data that servicing could move.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern unsigned short wia_upcase[65536];

LONG ref_compareunicodestrings(const wchar_t* s1, SIZE_T len1,
                               const wchar_t* s2, SIZE_T len2, BOOLEAN ci)
{
    SIZE_T n = (len1 < len2) ? len1 : len2;
    SIZE_T i;
    for (i = 0; i < n; ++i) {
        unsigned a = (unsigned short)s1[i];
        unsigned b = (unsigned short)s2[i];
        if (ci) { a = wia_upcase[a]; b = wia_upcase[b]; }
        if (a != b) return (LONG)a - (LONG)b;
    }
    return (LONG)((LONG_PTR)len1 - (LONG_PTR)len2);
}
