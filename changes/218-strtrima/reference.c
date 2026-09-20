// changes/218-strtrima/reference.c
// Oracle for shlwapi!StrTrimA. Not fast; just obviously right.
//
// The contract, measured in probes/trim.c against the live export:
//   * byte-wise on this code page, 0 of 254 byte values act as a DBCS lead byte after a trimmed
//     prefix, and 0 of 255 fail when used AS the trim character (ACP is 1252);
//   * both ends are trimmed; trim characters in the MIDDLE are left alone;
//   * the return is TRUE exactly when something was stripped;
//   * an all-trim string becomes empty and returns TRUE; an EMPTY source returns FALSE; an EMPTY set
//     returns FALSE and touches nothing; a NULL set returns FALSE and touches nothing; a NULL source
//     returns FALSE;
//   * It writes only what it must. The probe poisoned the bytes past the terminator and read them
//     back: "abc" trimmed of 'x' leaves the buffer completely untouched, "abcxx" gets exactly one
//     byte written (the new terminator) with the old 'x' and old terminator still in place, and
//     "xxabc" moves four bytes down and leaves the rest. Nothing is padded and nothing is cleared,
//     which is why correctness.c compares the whole buffer rather than the resulting string.
#include <windows.h>

int ref_strtrima(char* s, const char* set)
{
    int i, j, n, k;

    if (!s || !set) return 0;

    n = 0; while (s[n]) ++n;
    if (n == 0) return 0;

    /* first index whose character is NOT in the set */
    for (i = 0; i < n; ++i) {
        int in = 0;
        for (k = 0; set[k]; ++k) if (s[i] == set[k]) { in = 1; break; }
        if (!in) break;
    }
    if (i == n) {                      /* every character is a trim character */
        s[0] = 0;
        return 1;
    }
    /* last index whose character is NOT in the set */
    for (j = n - 1; j > i; --j) {
        int in = 0;
        for (k = 0; set[k]; ++k) if (s[j] == set[k]) { in = 1; break; }
        if (!in) break;
    }

    /* The order of the two writes is observable, and probes/diag.c caught it. The export
       terminates the TRAILING end first and only then moves the leading end down, so on
       "xxabcxx" trimmed of 'x' the buffer ends up
           a b c \0 c \0 x \0
       and not
           a b c \0 c  x  x \0
       -- the \0 at index 5 is the trailing cut, still visible after the move copied four bytes
       over the front. Trimming both ends therefore leaves TWO terminators behind, and only a
       whole-buffer comparison can see the difference. */
    if (j + 1 < n) s[j + 1] = 0;                     /* cut the trailing end, in place */
    if (i > 0) {
        int len = j - i + 2;                         /* the kept characters AND their terminator */
        int t;
        for (t = 0; t < len; ++t) s[t] = s[i + t];   /* forward copy: destination <= source */
    }
    return (i > 0 || j + 1 < n);
}
