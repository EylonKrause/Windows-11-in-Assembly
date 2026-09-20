// changes/214-strcspna/reference.c
// Oracle for shlwapi!StrCSpnA. Not fast; just obviously right.
//
// The contract, measured in probes/span.c against the live export:
//   * byte-wise on this code page, in both the subject and the set -- 0 of 254 byte values act as a
//     DBCS lead byte in the subject, and 0 of 252 cannot be a set member (ACP is 1252);
//   * returns the number of leading characters of pszStr that are NOT in pszSet, i.e. strlen when
//     none of them is;
//   * a NULL set is not the empty set: StrCSpnA("abc", NULL) is 0, while StrCSpnA("abc", "") is 3.
//     Either NULL argument returns 0;
//   * duplicates in the set are harmless; there is no length cap.
#include <windows.h>

int ref_strcspna(const char* s, const char* set)
{
    int i, j;

    if (!s || !set) return 0;           /* NOT the same as an empty set -- measured */

    for (i = 0; s[i]; ++i)
        for (j = 0; set[j]; ++j)
            if (s[i] == set[j]) return i;

    return i;                           /* no member found: the whole string */
}
