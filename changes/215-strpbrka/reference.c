// changes/215-strpbrka/reference.c
// Oracle for shlwapi!StrPBrkA. Not fast; just obviously right.
//
// The contract, measured in probes/span.c against the live export:
//   * byte-wise on this code page, in both the subject and the set -- 0 of 254 byte values act as a
//     DBCS lead byte in the subject, and 0 of 252 cannot be a set member (ACP is 1252);
//   * returns a pointer to the first character of pszStr that IS in pszSet, or NULL if none is;
//   * an EMPTY set returns NULL, and so does a NULL set -- unlike its sibling StrCSpnA, where the
//     two differ (0 versus strlen). Measured rather than assumed, because the two share a core;
//   * NULL subject returns NULL; an empty subject returns NULL;
//   * duplicates in the set are harmless; there is no length cap.
#include <windows.h>

const char* ref_strpbrka(const char* s, const char* set)
{
    int i, j;

    if (!s || !set) return 0;

    for (i = 0; s[i]; ++i)
        for (j = 0; set[j]; ++j)
            if (s[i] == set[j]) return s + i;

    return 0;
}
