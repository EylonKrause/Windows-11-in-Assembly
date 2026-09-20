// changes/219-pathstrippatha/reference.c
// Oracle for shlwapi!PathStripPathA: move the last component to the front, in place, using the
// separator rule change 212 derived by enumeration. Nothing past the new terminator is cleared.
//
// probes/strip.c verified the equivalence rather than assuming it, on both halves: over 87381
// strings in {a, backslash, slash, colon} and again over 488281 in
// {a, backslash, slash, colon, SPACE} (of which 400900 contain a space) the buffer left by the
// live PathStripPathA is byte for byte what you get by copying the live PathFindFileNameA answer to
// the front, with 0 mismatches, and the same holds for PathStripPathW.
#include <windows.h>

static int is2(char c) { return c == '\\' || c == '/'; }

void ref_pathstrippatha(char* s)
{
    size_t n = 0, last = 0, run = 0, i, k;

    if (!s) return;                      /* measured: NULL returns without faulting */
    while (s[n]) ++n;

    for (i = 0; i <= n; ++i)
    {
        if (i == n || is2(s[i]))
        {
            /* close the run [run, i): a LONE colon in it may separate */
            size_t colons = 0, at = 0, j;
            for (j = run; j < i; ++j) if (s[j] == ':') { ++colons; at = j; }
            if (colons == 1 && at + 1 < n && !is2(s[at + 1]) && at + 1 > last) last = at + 1;
            if (i < n)
            {
                if (i + 1 < n && !is2(s[i + 1]) && i + 1 > last) last = i + 1;
                run = i + 1;
            }
        }
    }
    if (!last) return;                   /* already at the front: nothing is written at all */

    k = 0;
    while (s[last + k]) { s[k] = s[last + k]; ++k; }
    s[k] = 0;                            /* and NOTHING past here is cleared */
}
