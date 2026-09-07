/* Derive shlwapi!PathRemoveFileSpecW by exhaustive enumeration.
   The function truncates the path at the last component. The unknown is where it refuses to cut
   (roots). Enumerate every string over {a, backslash, slash, colon} up to length 7 and dump the
   result as a truncation length, so the rule can be read off. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
typedef BOOL (WINAPI *F)(PWSTR);
static F S;
static const wchar_t AL[4] = { L'a', L'\\', L'/', L':' };
static wchar_t buf[32];

/* candidate rule: cut at the last backslash; if that backslash is at index 0 keep one; never cut a
   "X:" drive prefix. Parameterised so several variants can be scored at once. */
static int predict(const wchar_t* s, int n, int use_slash, int keep_root, int drive)
{
    int last = -1;
    for (int i = 0; i < n; ++i)
        if (s[i] == L'\\' || (use_slash && s[i] == L'/')) last = i;
    if (last < 0) return 0;                 /* no separator: truncate to empty */
    int cut = last;
    if (keep_root && cut == 0) cut = 1;     /* "\a" -> "\" */
    if (drive && cut == 2 && n >= 2 && s[1] == L':') cut = 3;
    return cut;
}
int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    S = (F)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "PathRemoveFileSpecW");
    if (!S) { printf("no export\n"); return 2; }

    /* first: does the return value simply say "did it change anything"? */
    { wchar_t d[32];
      wcscpy(d, L"C:\\a\\b"); BOOL r1 = S(d); printf("  [C:\\a\\b] -> %d [%ls]\n", !!r1, d);
      wcscpy(d, L"C:\\");     BOOL r2 = S(d); printf("  [C:\\]    -> %d [%ls]\n", !!r2, d);
      wcscpy(d, L"abc");      BOOL r3 = S(d); printf("  [abc]     -> %d [%ls]\n", !!r3, d);
      wcscpy(d, L"");         BOOL r4 = S(d); printf("  []        -> %d [%ls]\n", !!r4, d);
      wcscpy(d, L"\\a");      BOOL r5 = S(d); printf("  [\\a]      -> %d [%ls]\n", !!r5, d);
      wcscpy(d, L"a/b");      BOOL r6 = S(d); printf("  [a/b]     -> %d [%ls]\n", !!r6, d);
      wcscpy(d, L"\\\\srv\\share\\f"); BOOL r7 = S(d); printf("  [\\\\srv\\share\\f] -> %d [%ls]\n", !!r7, d);
      wcscpy(d, L"\\\\srv\\share");    BOOL r8 = S(d); printf("  [\\\\srv\\share]   -> %d [%ls]\n", !!r8, d);
    }
    printf("\nscoring candidate rules over all strings of length 0..7:\n");
    for (int us = 0; us < 2; ++us)
      for (int kr = 0; kr < 2; ++kr)
        for (int dr = 0; dr < 2; ++dr)
        {
            long long bad = 0, tested = 0;
            for (int n = 0; n <= 7; ++n)
            {
                long long t = 1; for (int i = 0; i < n; ++i) t *= 4;
                for (long long code = 0; code < t; ++code)
                {
                    long long c = code;
                    for (int i = 0; i < n; ++i) { buf[i] = AL[c & 3]; c >>= 2; }
                    buf[n] = 0;
                    S(buf);
                    int got = (int)wcslen(buf);
                    /* rebuild and compare against the prediction */
                    c = code;
                    for (int i = 0; i < n; ++i) { buf[i] = AL[c & 3]; c >>= 2; }
                    buf[n] = 0;
                    if (predict(buf, n, us, kr, dr) != got) ++bad;
                    ++tested;
                }
            }
            printf("  slash=%d keeproot=%d drive=%d : %lld mismatches over %lld\n", us, kr, dr, bad, tested);
        }
    return 0;
}
