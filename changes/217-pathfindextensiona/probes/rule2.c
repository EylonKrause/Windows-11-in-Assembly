/* changes/217-pathfindextensiona/probes/rule2.c
   The corrected PathFindExtension rule, verified by enumeration against BOTH live exports.

   probes/space.c established two things:
     * PathFindExtensionA and PathFindExtensionW agree with EACH OTHER on all 87381 strings over
       {a, '.', backslash, space} of length 0..8 -- so this is not an A/W asymmetry;
     * both disagree with change 132's rule on 14311 of them. Change 132 is LANDED, and its rule is
       incomplete: a space does something its 600k-fuzz corpus never produced.

   Reading the failures, the fix is small. Change 132 has the backward scan stop at a BACKSLASH:

       for (q = end; q > 0; ) { --q; if (p[q]=='.') return q; if (p[q]=='\\') break; }
       return end;

   and the measurements say a SPACE stops it too, on exactly the same footing -- "a.b " gives the
   terminator, "a.b\t" gives the dot, so it is 0x20 specifically and not whitespace in general. That
   matches the byte scan in space.c: of 255 values placed after the dot, exactly THREE stop it
   counting -- 0x20, 0x2E and 0x5C -- and the latter two are already explained by the last-dot and
   backslash rules.

   This probe proposes that one-character amendment and tests it exhaustively, on both exports, over
   the alphabet that makes every interaction reachable.                                            */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char*    (WINAPI *FA)(const char*);
typedef wchar_t* (WINAPI *FW)(const wchar_t*);
static FA fa;
static FW fw;

/* change 132's rule, as landed */
static int model_132(const char* p){
    int end = 0; while (p[end]) ++end;
    for (int q = end; q > 0; ) {
        --q;
        if (p[q] == '.')  return q;
        if (p[q] == '\\') break;
    }
    return end;
}

/* the amendment: a SPACE stops the backward scan on the same footing as a backslash */
static int model_fixed(const char* p){
    int end = 0; while (p[end]) ++end;
    for (int q = end; q > 0; ) {
        --q;
        if (p[q] == '.')  return q;
        if (p[q] == '\\' || p[q] == ' ') break;
    }
    return end;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    fa = (FA)GetProcAddress(hs, "PathFindExtensionA");
    fw = (FW)GetProcAddress(hs, "PathFindExtensionW");
    if (!fa || !fw) { printf("cannot resolve both exports\n"); return 1; }

    printf("Exhaustive over {a, '.', backslash, slash, colon, space}, lengths 0..8, BOTH exports.\n");
    printf("(6^0 + ... + 6^8 = 2015539 strings.)\n\n");
    {
        static const char AL[6] = { 'a', '.', '\\', '/', ':', ' ' };
        char s[12]; wchar_t w[12];
        long total = 0, aw = 0, a132 = 0, w132 = 0, afix = 0, wfix = 0;
        for (int len = 0; len <= 8; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 6;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 6]; w[i] = (wchar_t)s[i]; v /= 6; }
                s[len] = 0; w[len] = 0;
                int ra = (int)(fa(s) - s);
                int rw = (int)(fw(w) - w);
                int m1 = model_132(s);
                int m2 = model_fixed(s);
                if (ra != rw) { if (++aw <= 4) printf("  A/W DIFFER \"%s\": A=%d W=%d\n", s, ra, rw); }
                if (ra != m1) ++a132;
                if (rw != m1) ++w132;
                if (ra != m2) { if (++afix <= 6)
                    printf("  FIXED-rule MISMATCH (A) \"%s\": live=%d model=%d\n", s, ra, m2); }
                if (rw != m2) ++wfix;
                ++total;
            }
        }
        printf("\n  strings tested                             : %ld\n", total);
        printf("  A vs W (do the two exports agree?)         : %ld differences\n", aw);
        printf("  A vs change 132's LANDED rule              : %ld mismatches\n", a132);
        printf("  W vs change 132's LANDED rule              : %ld mismatches\n", w132);
        printf("  A vs the amended rule (space stops too)    : %ld mismatches\n", afix);
        printf("  W vs the amended rule (space stops too)    : %ld mismatches\n", wfix);
        printf("\n  => %s\n", (afix == 0 && wfix == 0)
               ? "THE AMENDMENT IS THE RULE, for BOTH exports. Change 132 must be corrected."
               : "the amendment is still incomplete -- keep digging");
    }

    printf("\n=== and once more with a high byte and a tab in the alphabet ===\n");
    printf("(0xE9 is an ordinary character here; the tab checks that it really is 0x20 alone and\n");
    printf(" not whitespace in general.)\n");
    {
        static const char A2[6] = { 'a', '.', '\\', ' ', '\t', (char)0xE9 };
        char s[12]; wchar_t w[12];
        long t2 = 0, b2 = 0, bw = 0;
        for (int len = 0; len <= 8; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 6;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = A2[v % 6]; w[i] = (wchar_t)(unsigned char)s[i]; v /= 6; }
                s[len] = 0; w[len] = 0;
                if ((int)(fa(s) - s) != model_fixed(s)) { if (++b2 <= 6)
                    printf("  MISMATCH (A) \"%s\"\n", s); }
                if ((int)(fw(w) - w) != model_fixed(s)) ++bw;
                ++t2;
            }
        }
        printf("  %ld strings: A %ld mismatches, W %ld mismatches\n", t2, b2, bw);
    }
    return 0;
}
