/* changes/212-pathfindfilenamea/probes/rule.c
   Does PathFindFileNameA follow the same separator rule as PathFindFileNameW?

   Change 161 did not guess the wide rule, it DERIVED it -- an earlier attempt at this repository had
   abandoned the function after four hypotheses failed, because the colon depends on RIGHT context:

     * '\' and '/' always separate. One sets the answer to i+1 when the next character is neither NUL
       nor '\' nor '/' (a following ':' is fine).
     * ':' sets the answer to i+1 under the same next-character test, but ONLY when it is the SOLE
       colon in its run -- the stretch between two backslash/slash characters. So ":a" gives 1 and
       "a:a" gives 2, while ":a:" and "a::a" both give 0.
     * the answer is the last position that set, or the start of the string.

   The obvious move is to reuse that rule for the narrow form. The obvious move is what this project
   keeps getting punished for: change 203 inherited 202's contract exactly, change 205's differed from
   ntdll's on the one detail that decided the implementation, and lstrcmpA turned out to be linguistic
   where its name suggested otherwise. pffa.c's spot checks are all consistent with a much SIMPLER
   rule that ignores the run condition entirely -- they simply never hit a two-colon run.

   So the rule is re-derived here the same way 161 derived it: EXHAUSTIVELY. Every string over
   {a, backslash, slash, colon} of length 0..9 is generated -- 349525 of them, the alphabet that makes
   every separator interaction reachable -- and the live export's answer is compared against both
   models. Whichever survives with zero mismatches is the contract.                                 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char* (WINAPI *A_P1)(const char*);
static A_P1 ffn;

static const char ALPHA[4] = { 'a', '\\', '/', ':' };

/* the rule change 161 derived for the WIDE form, transcribed to bytes */
static int model_161(const char* s){
    int ans = 0;
    int firstcolon = -1;     /* position of this run's first colon */
    int twocolons = 0;
    int i;
    for (i = 0; ; ++i) {
        char c = s[i];
        if (c == 0) break;
        if (c == ':') {
            if (firstcolon < 0) firstcolon = i; else twocolons = 1;
            continue;
        }
        if (c != '\\' && c != '/') continue;
        /* a backslash or slash closes the current run, then applies its own test */
        if (firstcolon >= 0 && !twocolons) {
            char n = s[firstcolon + 1];
            if (n != 0 && n != '\\' && n != '/') ans = firstcolon + 1;
        }
        firstcolon = -1; twocolons = 0;
        {
            char n = s[i + 1];
            if (n != 0 && n != '\\' && n != '/') ans = i + 1;
        }
    }
    if (firstcolon >= 0 && !twocolons) {
        char n = s[firstcolon + 1];
        if (n != 0 && n != '\\' && n != '/') ans = firstcolon + 1;
    }
    return ans;
}

/* the simpler rule pffa.c's spot checks were all consistent with: no run condition on the colon */
static int model_simple(const char* s){
    int ans = 0;
    for (int i = 0; s[i]; ++i) {
        char c = s[i];
        if (c != '\\' && c != '/' && c != ':') continue;
        char n = s[i + 1];
        if (n != 0 && n != '\\' && n != '/') ans = i + 1;
    }
    return ans;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    ffn = (A_P1)GetProcAddress(hs, "PathFindFileNameA");
    if (!ffn) { printf("no PathFindFileNameA\n"); return 1; }

    printf("Exhaustive over {a, backslash, slash, colon}, lengths 0..9.\n\n");

    long total = 0, bad161 = 0, badsimple = 0;
    char shown161 = 0, shownsimple = 0;
    char s[16];

    for (int len = 0; len <= 9; ++len) {
        long combos = 1;
        for (int i = 0; i < len; ++i) combos *= 4;
        for (long c = 0; c < combos; ++c) {
            long v = c;
            for (int i = 0; i < len; ++i) { s[i] = ALPHA[v & 3]; v >>= 2; }
            s[len] = 0;

            int live = (int)(ffn(s) - s);
            int m1 = model_161(s);
            int m2 = model_simple(s);
            ++total;
            if (m1 != live) {
                if (++bad161 <= 6) {
                    printf("  161-rule MISMATCH  \"%s\"  live=%d model=%d\n", s, live, m1);
                    shown161 = 1;
                }
            }
            if (m2 != live) {
                if (++badsimple <= 6) {
                    printf("  simple-rule MISMATCH \"%s\"  live=%d model=%d\n", s, live, m2);
                    shownsimple = 1;
                }
            }
        }
    }
    (void)shown161; (void)shownsimple;

    printf("\n  strings tested                       : %ld\n", total);
    printf("  mismatches vs the 161 (wide) rule    : %ld\n", bad161);
    printf("  mismatches vs the simple rule        : %ld\n", badsimple);
    printf("\n  => %s\n",
           bad161 == 0 ? "THE NARROW FORM CARRIES THE WIDE FORM'S RULE EXACTLY, run condition included."
                       : "the narrow form does NOT match the wide rule -- derive it separately");
    if (badsimple != 0 && bad161 == 0)
        printf("  => and the simple rule is WRONG (%ld mismatches): the colon's run condition is real,\n"
               "     and spot checks would never have found it.\n", badsimple);

    printf("\n=== a wider alphabet, to be sure nothing else is special ===\n");
    {
        static const char A2[8] = { 'a', '\\', '/', ':', '.', ' ', 'z', (char)0xE9 };
        long t2 = 0, b2 = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 8;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = A2[v & 7]; v >>= 3; }
                s[len] = 0;
                int live = (int)(ffn(s) - s);
                int m1 = model_161(s);
                ++t2;
                if (m1 != live) { if (++b2 <= 6) printf("  MISMATCH \"%s\" live=%d model=%d\n", s, live, m1); }
            }
        }
        printf("  %ld strings over {a,\\,/,:,.,space,z,0xE9}: %ld mismatches\n", t2, b2);
    }
    return 0;
}
