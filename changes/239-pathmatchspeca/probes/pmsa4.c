/* changes/239-pathmatchspeca/probes/pmsa4.c
   A TRUTH TABLE, because the model is failing in a shape no ordinary wildcard grammar has.

   pmsa3.c asserted the full model -- list, leading-space strip, "*.*", one spare trailing '?',
   60-class case fold, greedy backtracking -- and failed 7647 times out of 325 000. The failures are
   not a missing corner case; they contradict each other under every reading I can put on them:

       "a"  vs "**"    -> 1          but   "a"  vs "a**"  -> 0
       ""   vs "*"     -> 1          but   ""   vs "**"   -> 0
       "a"  vs "a*"    -> 1          but   "a"  vs "a*?"  -> 0
       ""   vs "?.*"   -> 1          and   ""   vs "?.?"  -> 1

   A trailing '*' is fine; a trailing '**' is not. A '?' matches the empty subject when a '.' is
   present and not otherwise. No greedy matcher, and no DOS_STAR/DOS_QM/DOS_DOT reading I know of,
   produces that set.

   So stop modelling and look. This file prints the COMPLETE truth table for small patterns against
   small subjects -- few enough rows to read directly -- rather than summarising it into a count. A
   summary is what hid this for two probes: pmsa2.c reported "191 are neither" and moved on, when
   "neither" was the interesting part.

   If the table turns out to be regular, the rule is in it. If it does not, that is the finding, and
   this change gets parked with the evidence rather than shipped on a model that fails 2% of the
   time -- which is what changes 228 and 230 did, and for the same reason. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *PMS)(LPCSTR, LPCSTR);
static PMS pms;
static int m(const char* s, const char* p){ return !!pms(s, p); }

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pms = (PMS)GetProcAddress(hs, "PathMatchSpecA");
    if (!pms) { printf("cannot resolve PathMatchSpecA\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    printf("=== 1. every pattern over {a, *, ?} to length 3, against \"\", \"a\", \"aa\", \"aaa\" ===\n");
    printf("  pattern |   \"\"   \"a\"  \"aa\" \"aaa\"\n");
    printf("  --------+------------------------\n");
    {
        static const char AL[3] = { 'a', '*', '?' };
        char p[8], s[8];
        for (int lp = 0; lp <= 3; ++lp) {
            long cp = 1; for (int i = 0; i < lp; ++i) cp *= 3;
            for (long kp = 0; kp < cp; ++kp) {
                long v = kp;
                for (int i = 0; i < lp; ++i) { p[i] = AL[v % 3]; v /= 3; }
                p[lp] = 0;
                printf("  %-7s |", lp ? p : "(empty)");
                for (int ls = 0; ls <= 3; ++ls) {
                    for (int i = 0; i < ls; ++i) s[i] = 'a';
                    s[ls] = 0;
                    printf("  %d   ", m(s, p));
                }
                printf("\n");
            }
        }
    }

    printf("\n=== 2. runs of '*' alone, and runs of '?' alone, by subject length ===\n");
    {
        char p[32], s[32];
        printf("  stars\\subject len:");
        for (int ls = 0; ls <= 5; ++ls) printf(" %d", ls);
        printf("\n");
        for (int n = 0; n <= 6; ++n) {
            for (int i = 0; i < n; ++i) p[i] = '*';
            p[n] = 0;
            printf("  %d star(s)         :", n);
            for (int ls = 0; ls <= 5; ++ls) {
                for (int i = 0; i < ls; ++i) s[i] = 'a';
                s[ls] = 0;
                printf(" %d", m(s, p));
            }
            printf("\n");
        }
        for (int n = 0; n <= 6; ++n) {
            for (int i = 0; i < n; ++i) p[i] = '?';
            p[n] = 0;
            printf("  %d question(s)     :", n);
            for (int ls = 0; ls <= 5; ++ls) {
                for (int i = 0; i < ls; ++i) s[i] = 'a';
                s[ls] = 0;
                printf(" %d", m(s, p));
            }
            printf("\n");
        }
    }

    printf("\n=== 3. 'a' followed by a run of '*', and by a run of '?' ===\n");
    {
        char p[32], s[32];
        for (int n = 0; n <= 5; ++n) {
            int k = 0; p[k++] = 'a';
            for (int i = 0; i < n; ++i) p[k++] = '*';
            p[k] = 0;
            printf("  \"a\" + %d star(s)  = %-8s :", n, p);
            for (int ls = 0; ls <= 4; ++ls) {
                for (int i = 0; i < ls; ++i) s[i] = 'a';
                s[ls] = 0;
                printf(" len%d:%d", ls, m(s, p));
            }
            printf("\n");
        }
        for (int n = 0; n <= 5; ++n) {
            int k = 0; p[k++] = 'a';
            for (int i = 0; i < n; ++i) p[k++] = '?';
            p[k] = 0;
            printf("  \"a\" + %d qm(s)    = %-8s :", n, p);
            for (int ls = 0; ls <= 4; ++ls) {
                for (int i = 0; i < ls; ++i) s[i] = 'a';
                s[ls] = 0;
                printf(" len%d:%d", ls, m(s, p));
            }
            printf("\n");
        }
    }

    printf("\n=== 4. does a '.' ANYWHERE in the pattern change what '*' and '?' do? ===\n");
    {
        static const char* PATS[] = { "*", "**", "?", "??", "*?", "?*", "*.", ".*", "?.", ".?",
                                      "*.*", "?.?", "*.?", "?.*", "..", ".", "a.", ".a", 0 };
        static const char* SUBS[] = { "", "a", "aa", "a.a", ".", "a.", ".a", 0 };
        printf("  pattern |");
        for (int j = 0; SUBS[j]; ++j) printf(" %-5s", SUBS[j][0] ? SUBS[j] : "(e)");
        printf("\n  --------+");
        for (int j = 0; SUBS[j]; ++j) printf("------");
        printf("\n");
        for (int i = 0; PATS[i]; ++i) {
            printf("  %-7s |", PATS[i]);
            for (int j = 0; SUBS[j]; ++j) printf(" %-5d", m(SUBS[j], PATS[i]));
            printf("\n");
        }
    }

    printf("\n=== 5. is there a LENGTH limit on the pattern or the subject? ===\n");
    {
        static char p[2048], s[2048];
        for (int n = 1; n <= 12; ++n) {
            int L = n * 60;
            for (int i = 0; i < L; ++i) s[i] = 'a';
            s[L] = 0;
            p[0] = '*'; p[1] = 'a'; p[2] = 0;
            int r1 = m(s, p);
            for (int i = 0; i < L; ++i) p[i] = 'a';
            p[L] = 0;
            int r2 = m(s, p);
            printf("  length %4d: \"*a\" -> %d,  all-literal pattern of the same length -> %d\n",
                   L, r1, r2);
        }
    }
    return 0;
}
