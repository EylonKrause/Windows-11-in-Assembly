// changes/212-pathfindfilenamea/reference.c
// Oracle for shlwapi!PathFindFileNameA, written from the rule probes/rule.c DERIVED by enumeration
// (see impl.asm): backslash and slash always separate; a colon separates only when it is the sole
// colon in its run. Not fast; just obviously right.
//
// The narrow form was NOT assumed to share the wide form's rule. 349525 strings over
// {a, backslash, slash, colon} of length 0..9 and 2396745 over a wider alphabet were compared
// against the live NARROW export: 0 mismatches against this rule, 76672 against the simpler rule
// that drops the colon's run condition.
#include <windows.h>

static int is2(char c) { return c == '\\' || c == '/'; }

const char* ref_pathfindfilenamea(const char* s)
{
    size_t n = 0;
    size_t last = 0;                 /* answer, as an index */
    size_t run  = 0;                 /* start of the current run */
    size_t i;

    if (!s) return 0;                /* measured: NULL in, NULL out */
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
    return s + last;
}
