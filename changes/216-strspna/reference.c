// changes/216-strspna/reference.c
// Oracle for shlwapi!StrSpnA. Not fast; just obviously right.
//
// The contract, measured in probes/span.c against the live export:
//   * byte-wise on this code page, in BOTH the subject and the set -- 0 of 254 byte values act as a
//     DBCS lead byte in the subject, and 0 of 252 cannot be a set member (ACP is 1252);
//   * returns the number of leading characters of pszStr that ARE in pszSet;
//   * an EMPTY set returns 0, and so does a NULL set -- here the two happen to agree, unlike the
//     sibling StrCSpnA where a NULL set gives 0 and an EMPTY set gives strlen. Measured rather than
//     assumed, because the three share a core;
//   * NULL subject returns 0; an empty subject returns 0;
//   * duplicates in the set are harmless; there is no length cap.
#include <windows.h>

int ref_strspna(const char* s, const char* set)
{
    int i, j;

    if (!s || !set) return 0;

    for (i = 0; s[i]; ++i) {
        int found = 0;
        for (j = 0; set[j]; ++j)
            if (s[i] == set[j]) { found = 1; break; }
        if (!found) return i;
    }
    return i;
}
