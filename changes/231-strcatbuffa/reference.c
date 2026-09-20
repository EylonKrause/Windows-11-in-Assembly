// changes/231-strcatbuffa/reference.c
// Independent oracle for shlwapi!StrCatBuffA.
//
// Every rule measured in probes/scb.c and probes/scb2.c against the live export:
//
//   * cch is the TOTAL buffer size; the result is capped at cch-1 characters.
//   * The destination scan is bounded by cch. If no terminator is found in the first cch bytes the
//     function writes nothing and returns the destination. That one rule also covers "the
//     destination is longer than the bound": its terminator lies outside the first cch bytes, so
//     the bounded scan never finds it, and nothing is written or truncated.
//   * It never writes at or beyond index cch -- 0 violations over 31 x 31 x 41 combinations.
//   * Once the scan succeeds it always stores the terminator, even when nothing is appended. In RAM
//     that store is invisible (zero over zero); a PAGE_READONLY destination proves it happens.
//   * A NULL source returns the destination and stores nothing (the NULL check precedes the store,
//     also shown read-only). A NULL destination returns NULL. cch <= 0 writes nothing.
//   * Byte-wise: 0 of 255 byte values disagree at each of four positions.
//   * When the caller LIES about cch it FAULTS rather than swallowing -- 37 of 37 -- so unlike
//     lstrcpy and lstrcat there is no wrapper and no NULL-on-fault return.

char* ref_strcatbuffa(char* dst, const char* src, int cch)
{
    if (!dst) return 0;
    if (!src) return dst;                 /* measured: no store */

    int at = -1;
    for (int i = 0; i < cch; ++i)         /* the scan is bounded by cch */
        if (dst[i] == 0) { at = i; break; }
    if (at < 0) return dst;               /* not terminated within cch: write NOTHING */

    int k = 0;
    while (at < cch - 1 && src[k]) dst[at++] = src[k++];
    dst[at] = 0;                          /* always, even when nothing was appended */
    return dst;
}
