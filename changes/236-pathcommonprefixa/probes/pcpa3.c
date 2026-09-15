/* changes/236-pathcommonprefixa/probes/pcpa3.c
   THE CUT. Where does PathCommonPrefixA truncate, once it knows how far the two paths agree?

   pcpa.c and pcpa2.c settled the comparison: a strictly 1:1 per-character equivalence over 256
   byte values, folding three ranges by -0x20 plus five singletons, with no expansions and no
   ignorables. What is left is the other half of the function, and it is the half the NAME hides:
   the answer is not the common CHARACTER prefix, it is that prefix cut back to a path boundary.

       "C:\aaa\bbb"  "C:\aaa\ccc"  ->  6   "C:\aaa"     separator DROPPED
       "C:\aaabbb"   "C:\aaaccc"   ->  3   "C:\"        separator KEPT
       "\\srv\shr\a" "\\srv\shr\b" ->  9   "\\srv\shr"  separator DROPPED
       "aaa\bbb"     "aaa\ccc"     ->  3   "aaa"        separator DROPPED

   Sometimes the trailing separator survives and sometimes it does not, and "it survives inside the
   root" is a guess until it is measured. It also cannot be measured by staring at a handful of
   paths, because the root of "\\srv\shr\x" is a different length from the root of "C:\x" and from
   the root of "\x", and a rule fitted to one of them is wrong on the others.

   THE TRICK THIS FILE USES. The truncation depends only on the COMMON PREFIX ITSELF, so it can be
   isolated: for any string P, the pair (P + "x", P + "y") agrees on exactly the first len(P)
   characters -- 'x' and 'y' are in different fold classes -- so

       trunc(P) == PathCommonPrefixA(P + "x", P + "y", NULL)

   That turns a two-argument function into a ONE-argument one, and then the whole rule can be
   enumerated rather than inferred: every string over {a, backslash, colon} up to length 8, which
   covers every arrangement of drive letters, roots, doubled separators, UNC shapes and empty
   components that fits in eight characters.

   Output is deliberately a RULE-FITTING report, not a dump: the cases where the answer is simply
   "the last separator, dropped" are counted, and only the cases that DIVERGE from that are listed,
   because those are the rule. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *PCP)(LPCSTR, LPCSTR, LPSTR);
static PCP pcp;

static int trunc_of(const char* p)
{
    char a[32], b[32];
    int n = (int)strlen(p);
    memcpy(a, p, n); a[n] = 'x'; a[n+1] = 0;
    memcpy(b, p, n); b[n] = 'y'; b[n+1] = 0;
    return pcp(a, b, NULL);
}

/* the candidate: the last separator, dropped */
static int last_sep_dropped(const char* p)
{
    int j = -1;
    for (int i = 0; p[i]; ++i) if (p[i] == '\\') j = i;
    return j < 0 ? 0 : j;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pcp = (PCP)GetProcAddress(hs, "PathCommonPrefixA");
    if (!pcp) { printf("cannot resolve PathCommonPrefixA\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    printf("=== 0. the isolation trick, checked against the cases pcpa.c measured directly ===\n");
    {
        struct { const char* p; int want; } V[] = {
            { "C:\\aaa\\", 6 }, { "C:\\aaa", 3 }, { "\\\\srv\\shr\\", 9 },
            { "aaa\\", 3 }, { "ab", 0 }, { "C:\\", 3 }, { "", 0 }, { 0, 0 }
        };
        for (int i = 0; V[i].p; ++i) {
            int got = trunc_of(V[i].p);
            printf("  trunc(\"%s\") = %d   %s\n", V[i].p, got,
                   got == V[i].want ? "" : "  <== does NOT match the two-argument measurement");
        }
    }

    printf("\n=== 1. EXHAUSTIVE trunc() over {a, backslash, colon} to length 8 ===\n");
    printf("  Baseline rule: \"the last separator, dropped\" (0 if there is none).\n");
    printf("  Only the strings that DIVERGE from that baseline are listed -- they are the rule.\n\n");
    {
        static const char AL[3] = { 'a', '\\', ':' };
        char p[16];
        long total = 0, agree = 0;
        long div_keep = 0, div_other = 0;
        int shown = 0;
        /* group the divergences by (what the baseline said) -> (what it actually is) */
        for (int len = 0; len <= 8; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 3;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { p[i] = AL[v % 3]; v /= 3; }
                p[len] = 0;
                int got = trunc_of(p);
                int base = last_sep_dropped(p);
                ++total;
                if (got == base) { ++agree; continue; }
                if (got == base + 1) ++div_keep; else ++div_other;
                if (shown < 60) {
                    printf("    \"%-8s\" baseline %d, actual %d   %s\n", p, base, got,
                           got == base + 1 ? "(separator KEPT)" : "(neither)");
                    ++shown;
                }
            }
        }
        printf("\n    %ld strings: %ld agree with \"last separator, dropped\"\n", total, agree);
        printf("    %ld keep the separator instead; %ld do neither\n", div_keep, div_other);
    }

    printf("\n=== 2. the same, restricted to the shapes that HAVE a root ===\n");
    printf("  For each root shape, a component is appended one character at a time and trunc() is\n");
    printf("  printed, so the exact position at which the separator stops being kept is visible.\n");
    {
        static const char* BASES[] = {
            "C:\\", "C:", "\\", "\\\\", "\\\\s", "\\\\srv", "\\\\srv\\", "\\\\srv\\shr",
            "\\\\srv\\shr\\", "a", "a\\", "aa\\bb", 0
        };
        for (int k = 0; BASES[k]; ++k) {
            char p[32];
            printf("  base \"%s\"\n", BASES[k]);
            for (int extra = 0; extra <= 5; ++extra) {
                int n = (int)strlen(BASES[k]);
                memcpy(p, BASES[k], n);
                for (int i = 0; i < extra; ++i) p[n+i] = 'q';
                p[n+extra] = 0;
                printf("      trunc(\"%-12s\") = %2d   (len %2d, last sep %2d)\n",
                       p, trunc_of(p), (int)strlen(p), last_sep_dropped(p));
            }
            /* and with a trailing separator after the component */
            {
                int n = (int)strlen(BASES[k]);
                memcpy(p, BASES[k], n);
                p[n]='q'; p[n+1]='q'; p[n+2]='\\'; p[n+3]=0;
                printf("      trunc(\"%-12s\") = %2d   (len %2d, last sep %2d)\n",
                       p, trunc_of(p), (int)strlen(p), last_sep_dropped(p));
            }
        }
    }

    printf("\n=== 3. does the FULL-MATCH case really skip the cut? ===\n");
    printf("  (pcpa.c saw \"C:\\\\aaa\" vs \"C:\\\\aaa\" -> 6 and \"C:\\\\aaa\\\\bbb\" vs \"C:\\\\aaa\" -> 6:\n");
    printf("   one is two identical strings, the other is one string ending at a separator in the\n");
    printf("   other. Both skip the truncation. This enumerates when that happens.)\n");
    {
        static const char AL[3] = { 'a', '\\', ':' };
        char a[16], b[16];
        long total = 0, skipped = 0, cut = 0;
        int shown = 0;
        for (int la = 0; la <= 6; ++la) {
            long ca = 1; for (int i = 0; i < la; ++i) ca *= 3;
            for (long ka = 0; ka < ca; ++ka) {
                long v = ka;
                for (int i = 0; i < la; ++i) { a[i] = AL[v % 3]; v /= 3; }
                a[la] = 0;
                /* b is a strict string-prefix of a */
                for (int lb = 0; lb < la; ++lb) {
                    memcpy(b, a, lb); b[lb] = 0;
                    int r = pcp(a, b, NULL);
                    ++total;
                    if (r == lb) ++skipped; else ++cut;
                    if (r == lb && lb > 0 && shown < 25) {
                        printf("    \"%-6s\" vs \"%-6s\" -> %d  (b is a prefix of a; NOT cut)"
                               "  a[%d]='%c'\n", a, b, r, lb, a[lb]);
                        ++shown;
                    }
                }
            }
        }
        printf("\n    %ld (a, strict string-prefix b) pairs: %ld returned len(b) uncut, %ld were cut\n",
               total, skipped, cut);
    }

    printf("\n=== 4. and the identical-string case ===\n");
    {
        static const char AL[3] = { 'a', '\\', ':' };
        char a[16];
        long total = 0, full = 0;
        for (int la = 0; la <= 7; ++la) {
            long ca = 1; for (int i = 0; i < la; ++i) ca *= 3;
            for (long ka = 0; ka < ca; ++ka) {
                long v = ka;
                for (int i = 0; i < la; ++i) { a[i] = AL[v % 3]; v /= 3; }
                a[la] = 0;
                if (pcp(a, a, NULL) == la) ++full;
                ++total;
            }
        }
        printf("    %ld identical pairs: %ld returned the full length (no cut at all)\n", total, full);
    }
    return 0;
}
