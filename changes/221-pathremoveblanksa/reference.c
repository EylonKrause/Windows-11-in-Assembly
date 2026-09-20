// changes/221-pathremoveblanksa/reference.c
// Oracle for shlwapi!PathRemoveBlanksA. Not fast; just obviously right.
//
// The contract, measured in probes/blanks.c against the live export:
//   * a BLANK is 0x20 and nothing else; every byte value was tried at both ends and exactly one
//     qualifies. a tab is not a blank: "\ta\t" comes back unchanged;
//   * both ends are stripped, blanks in the middle survive;
//   * a string made entirely of blanks becomes empty; an EMPTY string is left completely untouched,
//     which is a different thing;
//   * NULL returns without faulting;
//   * byte-wise, confirmed with the stronger screen that StrStrA failed: every byte value varied
//     where the function actually looks, 0 of 254 behaving unexpectedly.
//
// And the write order is the opposite of StrTrimA's (change 218). This one moves the leading end
// down first and cuts the trailing end afterwards, so "  abc  " leaves
//     a b c \0 <space> \0 <space> \0
// where cutting first would have left a 'c' at index 4. Nothing is ever padded or cleared, so
// correctness.c compares the whole buffer.
#include <windows.h>

void ref_pathremoveblanksa(char* s)
{
    int n = 0, i, j, t;

    if (!s) return;
    while (s[n]) ++n;
    if (n == 0) return;                      /* untouched */

    for (i = 0; i < n && s[i] == ' '; ++i) ;
    if (i == n) { s[0] = 0; return; }        /* all blanks */

    if (i > 0) {                             /* the leading move comes FIRST */
        int len = n - i + 1;                 /* the kept characters AND the terminator */
        for (t = 0; t < len; ++t) s[t] = s[i + t];
        n -= i;
    }
    for (j = n - 1; j >= 0 && s[j] == ' '; --j) ;
    if (j + 1 < n) s[j + 1] = 0;             /* only if blanks actually followed */
}
