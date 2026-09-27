/* changes/301-rtlisnameinunupcasedexpression/probes/oracle_check.c
   Validate reference.c against the live export before any assembly is written against it.

   discovery/wildcard_rule.c already proved the RULE over 1,019,564 cases, but it used a plain
   recursive matcher that only survives because the exhaustive alphabet tops out at four-character
   patterns. reference.c is the same rule with a memo table so it cannot go exponential, and the
   memo is exactly the kind of change that can quietly alter behaviour: a cell written before all
   of its dependencies are known, or a table not cleared between calls, produces an oracle that is
   wrong only on the long inputs the exhaustive sweep never reaches.

   So this re-runs the exhaustive sweep against the memoised version, and then does what the sweep
   cannot: random patterns and names long enough to matter, including the many-star shapes that are
   the whole reason for the change.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef BOOLEAN (NTAPI *PFN_WILD)(const USTR*, const USTR*, BOOLEAN, PWCH);
static PFN_WILD pWild;

int ref_name_in_expression(const unsigned short*, int, const unsigned short*, int);

static int sysmatch(const wchar_t* p, int pl, const wchar_t* n, int nl) {
    USTR e = { (USHORT)(pl * 2), (USHORT)(pl * 2 + 2), (PWSTR)p };
    USTR m = { (USHORT)(nl * 2), (USHORT)(nl * 2 + 2), (PWSTR)n };
    return pWild(&e, &m, FALSE, NULL) ? 1 : 0;
}
static int refmatch(const wchar_t* p, int pl, const wchar_t* n, int nl) {
    return ref_name_in_expression((const unsigned short*)p, pl * 2, (const unsigned short*)n, nl * 2);
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    pWild = (PFN_WILD)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlIsNameInUnUpcasedExpression");
    if (!pWild) { printf("missing export\n"); return 2; }

    long long tested = 0, diff = 0;

    /* 1. the exhaustive sweep again, now against the memoised oracle */
    {
        static const wchar_t PA[] = { L'a', L'.', L'*', L'?', L'<', L'>', L'"' };
        static const wchar_t NA[] = { L'a', L'b', L'.' };
        wchar_t pat[10], nam[10];
        for (int pl = 0; pl <= 4; ++pl) {
            long long pc_n = 1; for (int i = 0; i < pl; ++i) pc_n *= 7;
            for (long long pc = 0; pc < pc_n; ++pc) {
                long long t = pc;
                for (int i = 0; i < pl; ++i) { pat[i] = PA[t % 7]; t /= 7; }
                pat[pl] = 0;
                for (int nl = 0; nl <= 5; ++nl) {
                    long long nc_n = 1; for (int i = 0; i < nl; ++i) nc_n *= 3;
                    for (long long nc = 0; nc < nc_n; ++nc) {
                        long long u = nc;
                        for (int i = 0; i < nl; ++i) { nam[i] = NA[u % 3]; u /= 3; }
                        nam[nl] = 0;
                        int s = sysmatch(pat, pl, nam, nl), r = refmatch(pat, pl, nam, nl);
                        ++tested;
                        if (s != r) { if (diff < 10) printf("  DIFF pat=\"%ls\" name=\"%ls\" sys=%d ref=%d\n", pat, nam, s, r); ++diff; }
                    }
                }
            }
        }
        printf("  exhaustive (memoised oracle): %lld cases, %lld differ\n", tested, diff);
    }

    /* 2. random long cases, the shapes the sweep cannot reach */
    {
        static wchar_t pat[600], nam[9000];
        static const wchar_t PA[] = { L'a', L'b', L'.', L'*', L'?', L'<', L'>', L'"' };
        static const wchar_t NA[] = { L'a', L'b', L'.' };
        unsigned long seed = 0x301u;
        long long rtested = 0, rdiff = 0;
        for (int t = 0; t < 200000 && rdiff < 10; ++t) {
            seed = seed * 1103515245u + 12345u;
            int pl = (int)((seed >> 7) % 24);
            int nl = (int)((seed >> 13) % 60);
            /* bias a tenth of the draws toward pure '*'/'?' patterns, the common real shape */
            int pure = (((seed >> 3) & 7) == 0);
            for (int i = 0; i < pl; ++i) {
                seed = seed * 1103515245u + 12345u;
                pat[i] = pure ? PA[(seed >> 8) % 5] : PA[(seed >> 8) % 8];
            }
            pat[pl] = 0;
            for (int i = 0; i < nl; ++i) {
                seed = seed * 1103515245u + 12345u;
                nam[i] = NA[(seed >> 8) % 3];
            }
            nam[nl] = 0;
            int s = sysmatch(pat, pl, nam, nl), r = refmatch(pat, pl, nam, nl);
            ++rtested;
            if (s != r) { if (rdiff < 10) printf("  RANDOM DIFF pat=\"%ls\" name=\"%ls\" sys=%d ref=%d\n", pat, nam, s, r); ++rdiff; }
        }
        printf("  random pat<=23 name<=59:      %lld cases, %lld differ\n", rtested, rdiff);
        diff += rdiff; tested += rtested;
    }

    /* 3. the pathological shape the change exists for, at real length */
    {
        static wchar_t nam[4100];
        struct { const wchar_t* p; } P[] = {
            { L"*" }, { L"*.txt" }, { L"file*" }, { L"*abc*def*" }, { L"a*b*c*d*e*f*g*h" },
            { L"*a*a*a*a*a*" }, { L"<" }, { L"<.txt" }, { L"a>b" }, { L"*>*" }, { L"*\"*" },
        };
        int bad = 0;
        for (int i = 0; i < (int)(sizeof P / sizeof P[0]); ++i) {
            for (int nl = 1000; nl <= 4000; nl += 1000) {
                for (int k = 0; k < nl; ++k) nam[k] = (wchar_t)((k % 97 == 0) ? L'.' : (L'a' + (k % 26)));
                nam[nl] = 0;
                int pl = (int)wcslen(P[i].p);
                int s = sysmatch(P[i].p, pl, nam, nl), r = refmatch(P[i].p, pl, nam, nl);
                ++tested;
                if (s != r) { printf("  LONG DIFF pat=\"%ls\" nl=%d sys=%d ref=%d\n", P[i].p, nl, s, r); ++bad; ++diff; }
            }
        }
        printf("  long names 1000..4000:        %d pattern(s), %d differ\n",
               (int)(sizeof P / sizeof P[0]), bad);
    }

    printf("\n  TOTAL %lld cases, %lld differ%s\n", tested, diff,
           diff ? "" : "  -> reference.c reproduces the live export");
    return diff ? 1 : 0;
}
