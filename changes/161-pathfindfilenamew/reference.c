// changes/161-pathfindfilenamew/reference.c
// Oracle for shlwapi!PathFindFileNameW, written from the DERIVED rule (see impl.asm): backslash and
// slash always separate; a colon separates only when it is the sole colon in its run.
#include <windows.h>
#include <wchar.h>

static int wia_is2(wchar_t c) { return c == L'\\' || c == L'/'; }

const wchar_t* ref_pathfindfilenamew(const wchar_t* s)
{
    size_t n = 0;
    while (s[n]) ++n;

    size_t last = 0;                 /* answer, as an index */
    size_t run  = 0;                 /* start of the current run */
    for (size_t i = 0; i <= n; ++i)
    {
        if (i == n || wia_is2(s[i]))
        {
            /* close the run [run, i): a lone colon in it may separate */
            size_t colons = 0, at = 0;
            for (size_t j = run; j < i; ++j) if (s[j] == L':') { ++colons; at = j; }
            if (colons == 1 && at + 1 < n && !wia_is2(s[at + 1]) && at + 1 > last) last = at + 1;
            if (i < n)
            {
                if (i + 1 < n && !wia_is2(s[i + 1]) && i + 1 > last) last = i + 1;
                run = i + 1;
            }
        }
    }
    return s + last;
}
