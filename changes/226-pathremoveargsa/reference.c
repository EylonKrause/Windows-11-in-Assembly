// changes/226-pathremoveargsa/reference.c
// Independent oracle for shlwapi!PathRemoveArgsA -- the narrow sibling of change 175.
//
// Three behaviours, all re-derived against the NARROW export in probes/pra.c rather than inherited:
// exhaustive over {a, ' ', '"', TAB} to length 9 -- 349 525 strings -- with 0 mismatches, plus all
// 255 non-NUL byte values at seven positions the rule consults, also 0.
//
//   1. Find the first 0x20 OUTSIDE double quotes; each '"' toggles the state. Measured, not
//      assumed: sweeping every byte value, exactly one splits (0x20) and exactly one is trimmed
//      (0x20). A TAB does neither.
//   2. If one exists and something follows it: NUL it, and also NUL the last byte of that run of
//      spaces when a non-space follows. "ab   c" gets TWO terminators, at 2 and at 4 -- not at 2
//      and 3. The second write lands PAST the terminator, which no string comparison can see, so
//      every test compares the whole buffer.
//   3. If there is no unquoted space: trim trailing spaces, terminating at the first byte of the
//      trailing run. This ignores quoting entirely -- '"'+' ' IS cut even though that space sits
//      inside an unclosed quote, while '"'+' '+'a' is left alone.
//
// And when there is nothing to do, nothing is written -- not even a redundant terminator. That is
// observable against a poison fill, and it is why behaviour 3 tests the last byte before acting.
//
// There is no MAX_PATH guard: lengths 250..270 all act.

void ref_pathremoveargsa(char* psz)
{
    if (!psz) return;

    int n = 0;
    while (psz[n]) ++n;

    int q = 0, i = -1;
    for (int k = 0; k < n; ++k) {
        if (psz[k] == ' ' && !q) { i = k; break; }
        if (psz[k] == '"') q ^= 1;
    }

    if (i >= 0) {
        int args = i + 1;
        psz[i] = 0;
        if (psz[args] != 0) {               /* reads PAST the terminator just written */
            int j = args;
            while (psz[j] == ' ') ++j;
            if (psz[j] != 0) psz[j-1] = 0;  /* the LAST byte of the run */
        }
    } else {
        int j = n;
        while (j > 0 && psz[j-1] == ' ') --j;
        if (j < n) psz[j] = 0;              /* only when there WAS a trailing space */
    }
}
