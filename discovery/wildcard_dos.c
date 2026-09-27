/* discovery/wildcard_dos.c
   Are '<' and '>' exact aliases of '*' and '?' in RtlIsNameInUnUpcasedExpression, or do they carry
   the DOS-compatibility semantics that FsRtlIsNameInExpression gives them?

   discovery/wildcard_semantics.c settled the '*' and '?' rule exhaustively: over 85,995 cases a
   plain greedy matcher agrees with the export everywhere except five, and all five are one rule --
   a zero-length NAME matches only a zero-length expression, so "*" against "" is FALSE where every
   ordinary glob says TRUE.

   It also showed that '<' behaves like '*' and '>' like '?' on a first look, while '"' and '.' are
   literal. A first look is not enough. In FsRtlIsNameInExpression the DOS characters are not
   aliases at all: DOS_STAR '<' matches to the LAST dot rather than to the end, DOS_QM '>' matches
   zero characters when it runs off the end of the name, and DOS_DOT '"' matches a dot or nothing.
   Those differences only ever show up in names that contain dots, which is why a probe that tested
   "a" and "ab" could not see them.

   So this compares the export against ITSELF: every pattern is run once as written and once with
   '<' rewritten to '*' and '>' to '?'. If the two agree over an alphabet that includes dots, the
   DOS characters are aliases here and the implementation needs one code path. If they disagree,
   the first counterexample is printed, and the change gets a lot bigger.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef BOOLEAN (NTAPI *PFN_WILD)(const USTR*, const USTR*, BOOLEAN, PWCH);
static PFN_WILD pWild;

static int sysmatch(const wchar_t* pat, const wchar_t* name) {
    wchar_t pb[64], nb[64];
    size_t pn = wcslen(pat), nn = wcslen(name);
    memcpy(pb, pat, (pn + 1) * sizeof(wchar_t));
    memcpy(nb, name, (nn + 1) * sizeof(wchar_t));
    USTR e = { (USHORT)(pn * 2), (USHORT)((pn + 1) * 2), pb };
    USTR n = { (USHORT)(nn * 2), (USHORT)((nn + 1) * 2), nb };
    return pWild(&e, &n, FALSE, NULL) ? 1 : 0;
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    pWild = (PFN_WILD)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlIsNameInUnUpcasedExpression");
    if (!pWild) { printf("missing export\n"); return 2; }

    printf("== are '<' and '>' aliases of '*' and '?' when dots are in play? ==\n");
    {
        static const wchar_t PA[] = { L'a', L'.', L'*', L'?', L'<', L'>', L'"' };
        static const wchar_t NA[] = { L'a', L'.' };
        const int NP = 7;
        long long tested = 0, diff = 0;
        wchar_t pat[8], alias[8], nam[8];

        for (int pl = 0; pl <= 4; ++pl) {
            long long pcount = 1; for (int i = 0; i < pl; ++i) pcount *= NP;
            for (long long pc = 0; pc < pcount; ++pc) {
                long long t = pc;
                int hasdos = 0;
                for (int i = 0; i < pl; ++i) { pat[i] = PA[t % NP]; t /= NP; }
                pat[pl] = 0;
                for (int i = 0; i < pl; ++i) {
                    if      (pat[i] == L'<') { alias[i] = L'*'; hasdos = 1; }
                    else if (pat[i] == L'>') { alias[i] = L'?'; hasdos = 1; }
                    else                       alias[i] = pat[i];
                }
                alias[pl] = 0;
                if (!hasdos) continue;                 /* nothing to compare */

                for (int nl = 0; nl <= 5; ++nl) {
                    long long ncount = 1; for (int i = 0; i < nl; ++i) ncount *= 2;
                    for (long long nc = 0; nc < ncount; ++nc) {
                        long long u = nc;
                        for (int i = 0; i < nl; ++i) { nam[i] = NA[u & 1]; u >>= 1; }
                        nam[nl] = 0;
                        int a = sysmatch(pat, nam);
                        int b = sysmatch(alias, nam);
                        ++tested;
                        if (a != b) {
                            if (diff < 15)
                                printf("  DIFF pat=\"%ls\" alias=\"%ls\" name=\"%ls\"  dos=%d alias=%d\n",
                                       pat, alias, nam, a, b);
                            ++diff;
                        }
                    }
                }
            }
        }
        printf("  %lld comparisons, %lld differ%s\n", tested, diff,
               diff ? "  -> the DOS characters are NOT aliases" : "  -> '<' == '*' and '>' == '?' exactly");
    }

    printf("\n== is '\"' (DOS_DOT) literal, or does it match a dot / nothing? ==\n");
    {
        struct { const wchar_t* p; const wchar_t* n; } K[] = {
            { L"\"",   L"."  }, { L"\"",  L""   }, { L"\"",  L"a"  },
            { L"a\"",  L"a." }, { L"a\"", L"a"  }, { L"a\"b", L"a.b" },
            { L"a\"b", L"ab" },
        };
        for (int i = 0; i < (int)(sizeof K / sizeof K[0]); ++i)
            printf("  pat=%-6ls name=%-4ls -> %d\n",
                   K[i].p, K[i].n[0] ? K[i].n : L"(e)", sysmatch(K[i].p, K[i].n));
    }

    printf("\n== the zero-length-name rule, stated precisely ==\n");
    {
        const wchar_t* P[] = { L"", L"*", L"?", L"<", L">", L"\"", L"a", L"**", L"*?" };
        for (int i = 0; i < 9; ++i)
            printf("  pat=%-4ls vs empty name -> %d\n", P[i][0] ? P[i] : L"(e)", sysmatch(P[i], L""));
    }
    return 0;
}
