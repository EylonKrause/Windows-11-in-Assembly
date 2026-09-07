/* Dump every string of length <= 4 with its PathRemoveFileSpecW result, so the rule is readable. */
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
    for (int n = 1; n <= 4; ++n)
    {
        printf("--- length %d ---\n", n);
        long long t = 1; for (int i = 0; i < n; ++i) t *= 4;
        int percol = 0;
        for (long long code = 0; code < t; ++code)
        {
            long long c = code;
            for (int i = 0; i < n; ++i) orig[i] = AL[(c >> (2*i)) & 3];
            orig[n] = 0;
            wcscpy(buf, orig);
            BOOL r = S(buf);
            printf("  [%-5ls]->%d[%-5ls]", orig, !!r, buf);
            if (++percol % 4 == 0) printf("\n");
        }
        if (percol % 4) printf("\n");
    }
    return 0;
}
