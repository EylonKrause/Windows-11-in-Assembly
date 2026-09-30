/* changes/301-rtlisnameinunupcasedexpression/probes/oddfloor.c
   oddsuffix.c refuted byte arithmetic and left one reading: the "*" + literal-suffix fast path counts
   the NAME in floor(Length/2) characters while every other path counts ceil(Length/2).

   Decisive shape: n+1 wchars with ".txt" at positions n-4..n-1 and an extra 'q' at n, Length = 2n+1.
     floor view  "...q.txt"  -> the fast path would say TRUE
     ceil view   "...q.txtq" -> everything else says FALSE
   Then sweep: which exact pattern shapes take the floor path, across suffix lengths 1..6, one and
   several leading stars, and odd EXPRESSION lengths.
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
    const int n = 9;
    wchar_t nm[16];
    for (int i = 0; i <= n; ++i) nm[i] = L'q';
    nm[n-4] = L'.'; nm[n-3] = L't'; nm[n-2] = L'x'; nm[n-1] = L't';     /* nm[n] = 'q' straddles */
    USHORT odd = (USHORT)(2 * n + 1);

    printf("name wchars: qqqqq.txt + straddling 'q', Length = %u (odd)\n", odd);
    const wchar_t* P[] = { L"*.txt", L"*t", L"*xt", L"**.txt", L"*.txt*", L"*?txt", L"<.txt", L"*.txtq", L"*q" };
    for (int i = 0; i < (int)(sizeof P / sizeof P[0]); ++i)
        printf("  %-8ls -> %d\n", P[i], m(P[i], (USHORT)(wcslen(P[i]) * 2), nm, odd));

    printf("\nexhaustive: pattern '*' + suffix over {q . t x} of length 1..4, name above (odd Length):\n");
    printf("  a pattern agreeing with FLOOR but not CEIL marks the fast path\n");
    static const wchar_t A[] = { L'q', L'.', L't', L'x' };
    int floorOnly = 0, ceilOnly = 0, both = 0, neither = 0;
    for (int len = 1; len <= 4; ++len) {
        int tot = 1; for (int i = 0; i < len; ++i) tot *= 4;
        for (int c = 0; c < tot; ++c) {
            wchar_t pat[8]; int t = c; pat[0] = L'*';
            for (int i = 0; i < len; ++i) { pat[1 + i] = A[t % 4]; t /= 4; }
            pat[1 + len] = 0;
            int s = m(pat, (USHORT)((1 + len) * 2), nm, odd);
            /* floor name = nm[0..n-1], ceil name = nm[0..n] */
            int fl = (len <= n) && !memcmp(nm + n - len, pat + 1, len * 2);
            int ce = (len <= n + 1) && !memcmp(nm + n + 1 - len, pat + 1, len * 2);
            if (s == fl && s != ce) ++floorOnly; else if (s == ce && s != fl) ++ceilOnly; else if (s == fl && s == ce) ++both; else ++neither;
        }
    }
    printf("  agrees with floor only: %d   ceil only: %d   both: %d   NEITHER: %d\n", floorOnly, ceilOnly, both, neither);
    return 0;
}
