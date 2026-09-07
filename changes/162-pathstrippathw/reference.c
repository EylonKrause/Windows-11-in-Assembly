// changes/162-pathstrippathw/reference.c
// Oracle for shlwapi!PathStripPathW: move the last component to the front, in place, using the
// separator rule derived in change 161. Nothing past the new terminator is cleared.
#include <windows.h>
#include <wchar.h>

static int wia_is2(wchar_t c) { return c == L'\\' || c == L'/'; }

void ref_pathstrippathw(wchar_t* s)
{
    size_t n = 0;
    while (s[n]) ++n;

    size_t last = 0, run = 0;
    for (size_t i = 0; i <= n; ++i)
    {
        if (i == n || wia_is2(s[i]))
        {
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
    if (!last) return;
    size_t k = 0;
    while (s[last + k]) { s[k] = s[last + k]; ++k; }
    s[k] = 0;
}
