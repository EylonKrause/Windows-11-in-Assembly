/* discovery/wildcard_semantics.c
   What exactly does RtlIsNameInUnUpcasedExpression treat as a wildcard, and does a plain greedy
   matcher reproduce it?

   discovery/ntdll_ntcopy_wildcard.c found the reason to ask:

     pattern            name=16     name=64    name=256   name=1024
     "*"                5.78 ns     5.78 ns     5.78 ns     5.78 ns
     "file*"            9.56        9.56        9.56        9.56
     "*abc*def*"      369.14     1679.30     6876.56    27748.44
     "a*b*c*d*e*f*g*h" 956.45     4576.56    18889.06    76520.31

   A single leading or trailing star is flat and cheap. An INTERIOR star costs about 27 nanoseconds
   per character of name, roughly a hundred cycles each, and eight of them cost 76 microseconds to
   match one file name. That is a backtracking matcher, and a two-pointer greedy one does the same
   job in linear time. With IgnoreCase = FALSE the routine touches no case table, so unlike most of
   the name-matching surface this one is locale-free and reproducible.

   Before any of that matters the SEMANTICS have to be pinned down, because Windows name matching
   has more wildcards than '*' and '?'. The DOS-compatibility set -- DOS_STAR '<', DOS_QM '>',
   DOS_DOT '"' -- is handled by FsRtlIsNameInExpression and may or may not be handled here, and
   getting that wrong would produce a matcher that is fast and quietly wrong on exactly the inputs
   the compatibility path exists for.

   So this probe does two things:
     1. asks which of the candidate characters actually behave as wildcards, one at a time;
     2. runs an EXHAUSTIVE comparison against a plain greedy '*'/'?' reference over every pattern
        and every name in a small alphabet, and reports the first disagreement rather than a count,
        because one counterexample is what decides whether the simple rule is the real rule.

   NOTHING HERE IS A CONTRACT until the exhaustive pass is clean.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef BOOLEAN (NTAPI *PFN_WILD)(const USTR*, const USTR*, BOOLEAN, PWCH);
static PFN_WILD pWild;

static BOOLEAN sysmatch(const wchar_t* pat, const wchar_t* name) {
    wchar_t pb[64], nb[64];
    size_t pn = wcslen(pat), nn = wcslen(name);
    memcpy(pb, pat, (pn + 1) * sizeof(wchar_t));
    memcpy(nb, name, (nn + 1) * sizeof(wchar_t));
    USTR e = { (USHORT)(pn * 2), (USHORT)((pn + 1) * 2), pb };
    USTR n = { (USHORT)(nn * 2), (USHORT)((nn + 1) * 2), nb };
    return pWild(&e, &n, FALSE, NULL);
}

/* the plain rule: '*' matches any run including empty, '?' matches exactly one character */
static int refmatch(const wchar_t* p, const wchar_t* n) {
    const wchar_t *star = 0, *mark = 0;
    while (*n) {
        if (*p == L'?' || *p == *n) { ++p; ++n; }
        else if (*p == L'*') { star = p++; mark = n; }
        else if (star) { p = star + 1; n = ++mark; }
        else return 0;
    }
    while (*p == L'*') ++p;
    return *p == 0;
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    pWild = (PFN_WILD)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlIsNameInUnUpcasedExpression");
    if (!pWild) { printf("missing export\n"); return 2; }

    printf("== 1. which characters behave as a wildcard? ==\n");
    {
        /* a one-character pattern that matches a name it is not equal to means it is special */
        static const wchar_t C[]  = { L'*', L'?', L'<', L'>', L'"', L'.', L'a' };
        static const char*   NM[] = { "*", "?", "< DOS_STAR", "> DOS_QM", "\" DOS_DOT", ".", "a (control)" };
        for (int i = 0; i < 7; ++i) {
            wchar_t p[2] = { C[i], 0 };
            int m_a   = sysmatch(p, L"a");
            int m_ab  = sysmatch(p, L"ab");
            int m_nil = sysmatch(p, L"");
            printf("  %-12s vs \"a\"=%d  \"ab\"=%d  \"\"=%d\n", NM[i], m_a, m_ab, m_nil);
        }
    }

    printf("\n== 2. exhaustive vs a plain greedy '*'/'?' matcher ==\n");
    {
        static const wchar_t PA[] = { L'a', L'b', L'*', L'?' };
        static const wchar_t NA[] = { L'a', L'b' };
        long long tested = 0, diff = 0;
        wchar_t pat[8], nam[8];
        for (int pl = 0; pl <= 5; ++pl) {
            long long pcount = 1; for (int i = 0; i < pl; ++i) pcount *= 4;
            for (long long pc = 0; pc < pcount; ++pc) {
                long long t = pc;
                for (int i = 0; i < pl; ++i) { pat[i] = PA[t & 3]; t >>= 2; }
                pat[pl] = 0;
                for (int nl = 0; nl <= 5; ++nl) {
                    long long ncount = 1; for (int i = 0; i < nl; ++i) ncount *= 2;
                    for (long long nc = 0; nc < ncount; ++nc) {
                        long long u = nc;
                        for (int i = 0; i < nl; ++i) { nam[i] = NA[u & 1]; u >>= 1; }
                        nam[nl] = 0;
                        int s = sysmatch(pat, nam) ? 1 : 0;
                        int r = refmatch(pat, nam);
                        ++tested;
                        if (s != r) {
                            if (diff < 12) printf("  DIFF pat=\"%ls\" name=\"%ls\"  sys=%d ref=%d\n", pat, nam, s, r);
                            ++diff;
                        }
                    }
                }
            }
        }
        printf("  %lld cases, %lld differ%s\n", tested, diff,
               diff ? "" : "  -> the plain greedy rule IS the rule over this alphabet");
    }

    printf("\n== 3. the empty-pattern and empty-name corners ==\n");
    {
        struct { const wchar_t* p; const wchar_t* n; } K[] = {
            { L"",    L""    }, { L"",   L"a"  }, { L"*",  L""   }, { L"?",  L""  },
            { L"**",  L""    }, { L"**", L"ab" }, { L"a*", L"a"  }, { L"*a", L"a" },
            { L"?*",  L"a"   }, { L"*?", L"a"  }, { L"a?", L"a"  },
        };
        for (int i = 0; i < (int)(sizeof K / sizeof K[0]); ++i) {
            int s = sysmatch(K[i].p, K[i].n) ? 1 : 0, r = refmatch(K[i].p, K[i].n);
            printf("  pat=%-5ls name=%-3ls sys=%d ref=%d %s\n",
                   K[i].p[0] ? K[i].p : L"(e)", K[i].n[0] ? K[i].n : L"(e)", s, r, s == r ? "" : " <<< DIFF");
        }
    }
    return 0;
}
