/* changes/301-rtlisnameinunupcasedexpression/probes/oddsuffix.c
   With an odd Name->Length, "*" + literal suffix disagrees with the ceil(Length/2) walk that every
   other shape follows. Hypothesis: the star+suffix fast path locates the suffix with BYTE arithmetic,
   Buffer + Length - SuffixBytes, which on an odd Length is a window shifted by one byte.

   The test that decides it: a name whose aligned last characters are NOT the suffix, but whose bytes
   one position over spell it exactly. If the export says TRUE there, it compared bytes, not wchars.
   The same name is run through every shape that might or might not take that path.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef BOOLEAN (NTAPI *PFN_WILD)(const USTR*, const USTR*, BOOLEAN, PWCH);
static PFN_WILD pW;

static int m(const wchar_t* p, USHORT pb, const wchar_t* n, USHORT nb) {
    USTR e = { pb, pb, (PWSTR)p }, x = { nb, nb, (PWSTR)n };
    return pW(&e, &x, FALSE, NULL) ? 1 : 0;
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    pW = (PFN_WILD)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlIsNameInUnUpcasedExpression");

    /* n+1 wchars present; Length = 2n+1. Byte window [2n+1-8, 2n+1) spells ".txt" in UTF-16LE:
       hi(w[n-4]) = 2E, w[n-3] = 7400, w[n-2] = 7800, w[n-1] = 7400, lo(w[n]) = 00 */
    wchar_t shifted[16];
    const int n = 9;
    for (int i = 0; i <= n; ++i) shifted[i] = L'q';
    shifted[n-4] = 0x2E41;  /* hi byte 2E */
    shifted[n-3] = 0x7400;
    shifted[n-2] = 0x7800;
    shifted[n-1] = 0x7400;
    shifted[n]   = 0x0000;
    USHORT oddLen = (USHORT)(2 * n + 1);

    /* control: aligned ".txt" at the end, odd Length */
    wchar_t aligned[16];
    for (int i = 0; i <= n; ++i) aligned[i] = L'q';
    aligned[n-3] = L'.'; aligned[n-2] = L't'; aligned[n-1] = L'x'; aligned[n] = L't';

    const wchar_t* P[] = { L"*.txt", L"**.txt", L"*?txt", L"*.t?t", L"<.txt", L"*t", L"*.txt*", L"a*.txt", L"*.TXT" };
    printf("%-9s  %-28s %-28s %-24s\n", "pattern", "byte-shifted .txt, Len odd", "aligned .txt, Len odd", "aligned .txt, Len even");
    for (int i = 0; i < (int)(sizeof P / sizeof P[0]); ++i) {
        USHORT pb = (USHORT)(wcslen(P[i]) * 2);
        printf("%-9ls  %-28d %-28d %-24d\n", P[i],
               m(P[i], pb, shifted, oddLen),
               m(P[i], pb, aligned, oddLen),
               m(P[i], pb, aligned, (USHORT)(2 * n + 2)));
    }

    /* odd EXPRESSION length on the fast path: does the suffix length come from bytes too? */
    printf("\nodd expression Length, pattern \"*.txt\" + trailing wchar:\n");
    wchar_t pat[8] = { L'*', L'.', L't', L'x', L't', 0x4100, 0 };
    printf("  pattern Length 11, name aligned '.txt' Len even : %d\n", m(pat, 11, aligned, (USHORT)(2*n+2)));
    printf("  pattern Length 11, name byte-shifted     Len odd  : %d\n", m(pat, 11, shifted, oddLen));
    return 0;
}
