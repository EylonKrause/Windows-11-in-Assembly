// changes/233-pathquotespacesa/reference.c
// Independent oracle for shlwapi!PathQuoteSpacesA.
//
// Every rule re-derived against the NARROW export in probes/pqsa.c, not inherited from change 172:
//
//   * exactly ONE byte value counts as a space: 0x20. Sweeping all 255 non-NUL values in the middle
//     of a path, only that one makes it quote -- a TAB does not.
//   * The length cap is 257, measured by sweeping lengths 1..400 with one space: the last length
//     that quotes is 257, the first that does not is 258.
//   * On failure the buffer is UNTOUCHED (0 of 143 over-long cases modified a byte; nor did the
//     no-space case).
//   * An already-quoted path is quoted AGAIN.
//   * NULL returns 0 without faulting.
//   * 0 mismatches over all 87381 strings of {a, SPACE, quote, TAB} to length 8.
//
// A buffer too small for the result FAULTS rather than being swallowed, so there is no wrapper and
// no failure value to model here.

int ref_pathquotespacesa(char* psz)
{
    if (!psz) return 0;

    int n = 0;
    while (psz[n]) ++n;

    int hasspace = 0;
    for (int i = 0; i < n; ++i) if (psz[i] == 0x20) { hasspace = 1; break; }
    if (!hasspace || n > 257) return 0;

    for (int i = n; i >= 0; --i) psz[i+1] = psz[i];   /* backwards: the regions overlap */
    psz[0]   = 0x22;
    psz[n+1] = 0x22;
    psz[n+2] = 0;
    return 1;
}
