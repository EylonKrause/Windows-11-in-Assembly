/* Classify PathRemoveFileSpecW's truncation length relative to the last backslash. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
typedef BOOL (WINAPI *F)(PWSTR);
static F S;
static const wchar_t AL[4] = { L'a', L'\\', L'/', L':' };
static wchar_t buf[32], orig[32];
int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    S = (F)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "PathRemoveFileSpecW");
    long long cls_eq = 0, cls_p1 = 0, cls_other = 0, cls_nosep = 0;
    int shown = 0;
    printf("classification of the truncation length L vs the last backslash index p:\n");
    for (int n = 0; n <= 7; ++n)
    {
        long long t = 1; for (int i = 0; i < n; ++i) t *= 4;
        for (long long code = 0; code < t; ++code)
        {
            long long c = code;
            for (int i = 0; i < n; ++i) orig[i] = AL[(c >> (2*i)) & 3];
            orig[n] = 0;
            wcscpy(buf, orig);
            S(buf);
            int L = (int)wcslen(buf);
            int p = -1;
            for (int i = 0; i < n; ++i) if (orig[i] == L'\\') p = i;
            if (p < 0) { if (L != 0) { ++cls_other; if (shown < 12) { printf("  NOSEP  [%-8ls] L=%d\n", orig, L); ++shown; } }
                         else ++cls_nosep; continue; }
            if (L == p) ++cls_eq;
            else if (L == p + 1) ++cls_p1;
            else { ++cls_other; if (shown < 12) { printf("  OTHER  [%-8ls] p=%d L=%d\n", orig, p, L); ++shown; } }
        }
    }
    printf("\n  no backslash, truncated to empty : %lld\n", cls_nosep);
    printf("  L == last backslash index        : %lld\n", cls_eq);
    printf("  L == last backslash index + 1    : %lld\n", cls_p1);
    printf("  anything else                    : %lld\n", cls_other);

    /* when is it p+1 rather than p? dump a sample of each, grouped by the prefix */
    printf("\nsample of L == p+1 (the 'this is a root, keep the backslash' cases):\n");
    shown = 0;
    for (int n = 0; n <= 6 && shown < 25; ++n)
    {
        long long t = 1; for (int i = 0; i < n; ++i) t *= 4;
        for (long long code = 0; code < t && shown < 25; ++code)
        {
            long long c = code;
            for (int i = 0; i < n; ++i) orig[i] = AL[(c >> (2*i)) & 3];
            orig[n] = 0;
            wcscpy(buf, orig);
            S(buf);
            int L = (int)wcslen(buf);
            int p = -1;
            for (int i = 0; i < n; ++i) if (orig[i] == L'\\') p = i;
            if (p >= 0 && L == p + 1) { printf("  [%-8ls] p=%d -> [%ls]\n", orig, p, buf); ++shown; }
        }
    }
    return 0;
}
