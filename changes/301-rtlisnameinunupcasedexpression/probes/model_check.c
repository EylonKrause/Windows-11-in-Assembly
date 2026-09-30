/* changes/301-rtlisnameinunupcasedexpression/probes/model_check.c
   model.c (the algorithm the assembly will implement) against the live export and reference.c,
   over the same three passes oracle_check.c used, plus long names that force the column DP across
   many 64-bit words and the greedy skip across many 16-character blocks.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef BOOLEAN (NTAPI *PFN_WILD)(const USTR*, const USTR*, BOOLEAN, PWCH);
static PFN_WILD pWild;

int ref_name_in_expression(const unsigned short*, int, const unsigned short*, int);
int model_match(const unsigned short*, int, const unsigned short*, int);

static long long tested, bad;

static void one(const wchar_t* p, int pl, const wchar_t* n, int nl) {
    USTR e = { (USHORT)(pl * 2), (USHORT)(pl * 2 + 2), (PWSTR)p };
    USTR m = { (USHORT)(nl * 2), (USHORT)(nl * 2 + 2), (PWSTR)n };
    int s = pWild(&e, &m, FALSE, NULL) ? 1 : 0;
    int mo = model_match((const unsigned short*)p, pl, (const unsigned short*)n, nl);
    ++tested;
    if (s != mo) {
        if (bad < 12) {
            int r = (pl <= 512 && nl <= 8192) ? ref_name_in_expression((const unsigned short*)p, pl*2, (const unsigned short*)n, nl*2) : -1;
            printf("  DIFF pl=%d nl=%d pat=\"%.*ls\" name=\"%.*ls\" sys=%d model=%d ref=%d\n",
                   pl, nl, pl > 40 ? 40 : pl, p, nl > 60 ? 60 : nl, n, s, mo, r);
        }
        ++bad;
    }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    pWild = (PFN_WILD)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlIsNameInUnUpcasedExpression");

    /* 1. exhaustive */
    {
        static const wchar_t PA[] = { L'a', L'.', L'*', L'?', L'<', L'>', L'"' };
        static const wchar_t NA[] = { L'a', L'b', L'.' };
        wchar_t pat[10], nam[10];
        for (int pl = 0; pl <= 4; ++pl) {
            long long pc_n = 1; for (int i = 0; i < pl; ++i) pc_n *= 7;
            for (long long pc = 0; pc < pc_n; ++pc) {
                long long t = pc;
                for (int i = 0; i < pl; ++i) { pat[i] = PA[t % 7]; t /= 7; }
                for (int nl = 0; nl <= 5; ++nl) {
                    long long nc_n = 1; for (int i = 0; i < nl; ++i) nc_n *= 3;
                    for (long long nc = 0; nc < nc_n; ++nc) {
                        long long u = nc;
                        for (int i = 0; i < nl; ++i) { nam[i] = NA[u % 3]; u /= 3; }
                        one(pat, pl, nam, nl);
                    }
                }
            }
        }
        printf("  exhaustive:        %lld cases, %lld differ\n", tested, bad);
    }

    /* 2. random, pattern up to 23, name up to 200 -- crosses several DP words */
    {
        static wchar_t pat[64], nam[512];
        static const wchar_t PA[] = { L'a', L'b', L'.', L'*', L'?', L'<', L'>', L'"' };
        static const wchar_t NA[] = { L'a', L'b', L'.' };
        unsigned long seed = 0x3011u;
        long long t0 = tested, b0 = bad;
        for (int t = 0; t < 400000 && bad < 12; ++t) {
            seed = seed * 1103515245u + 12345u;
            int pl = (int)((seed >> 7) % 24);
            int nl = (int)((seed >> 13) % 201);
            int pure = (((seed >> 3) & 3) == 0);
            for (int i = 0; i < pl; ++i) { seed = seed * 1103515245u + 12345u; pat[i] = pure ? PA[(seed >> 8) % 5] : PA[(seed >> 8) % 8]; }
            for (int i = 0; i < nl; ++i) { seed = seed * 1103515245u + 12345u; nam[i] = NA[(seed >> 8) % 3]; }
            one(pat, pl, nam, nl);
        }
        printf("  random pl<=23 nl<=200: %lld cases, %lld differ\n", tested - t0, bad - b0);
    }

    /* 3. long names, every shape the bench will use and every DP-word boundary */
    {
        static wchar_t nam[4200];
        static const wchar_t* P[] = {
            L"*", L"*.txt", L"*.zzz", L"file*", L"zzzz*", L"*.*", L"????", L"*abc*def*", L"*q*",
            L"<.txt", L"<\"*", L"<", L"*.>>>", L"a<b<c<d", L"f<", L"f*t", L"*t", L"<t", L"*>", L">*",
            L"\"*", L"*\"", L"*.<", L"<.<", L"f?le*", L"*?*?*", L"<<<<", L">>>>",
        };
        long long t0 = tested, b0 = bad;
        for (int i = 0; i < (int)(sizeof P / sizeof P[0]); ++i) {
            int pl = (int)wcslen(P[i]);
            for (int nl = 1; nl <= 4100; nl += (nl < 200 ? 1 : 61)) {
                for (int dots = 0; dots < 3; ++dots) {
                    for (int j = 0; j < nl; ++j) nam[j] = (wchar_t)(L'a' + (j * 7 % 26));
                    if (dots >= 1 && nl >= 5) { nam[nl-4] = L'.'; nam[nl-3] = L't'; nam[nl-2] = L'x'; nam[nl-1] = L't'; }
                    if (dots >= 2) { for (int j = 13; j < nl; j += 97) nam[j] = L'.'; if (nl > 1) nam[nl-1] = L'.'; }
                    if (nl > 4) { nam[0] = L'f'; nam[1] = L'i'; nam[2] = L'l'; nam[3] = L'e'; }
                    one(P[i], pl, nam, nl);
                }
            }
        }
        printf("  long names 1..4100: %lld cases, %lld differ\n", tested - t0, bad - b0);
    }

    printf("\n  TOTAL %lld cases, %lld differ%s\n", tested, bad, bad ? "" : "  -> the algorithm reproduces the live export");
    return bad ? 1 : 0;
}
