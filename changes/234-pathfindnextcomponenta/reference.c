// changes/234-pathfindnextcomponenta/reference.c
// Independent oracle for shlwapi!PathFindNextComponentA.
//
// Every rule re-derived against the NARROW export in probes/pfnca.c, not inherited from change 173:
//
//   * NULL and the EMPTY STRING both return NULL, and those are the only NULLs.
//   * exactly ONE byte value is a separator: 0x5C. Sweeping all 255 non-NUL values between two
//     letters, only the backslash moves the answer -- a forward slash is not a separator.
//   * With no separator the answer is a pointer to the TERMINATOR, not NULL.
//   * The doubled-separator quirk: when the byte after the first separator is also a separator,
//     advance exactly ONE more, never the whole run. Measured with leading runs of 1..6: the
//     offset is 1, then 2, and stays 2 however long the run gets. "Skip the run" is the obvious
//     implementation and it is wrong from three separators onward.
//   * 0 mismatches over all 349525 strings of {a, backslash, /, 0x80} to length 9.

char* ref_pathfindnextcomponenta(const char* psz)
{
    if (!psz) return 0;
    if (!psz[0]) return 0;

    const char* s = 0;
    for (int i = 0; psz[i]; ++i)
        if (psz[i] == 0x5C) { s = psz + i; break; }

    if (s) {
        if (s[1] == 0x5C) ++s;            /* exactly one extra, not the whole run */
        return (char*)(s + 1);
    }
    int n = 0;
    while (psz[n]) ++n;
    return (char*)(psz + n);              /* the terminator */
}
