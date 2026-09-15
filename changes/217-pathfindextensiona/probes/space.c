/* changes/217-pathfindextensiona/probes/space.c
   A SPACE does something to PathFindExtension that change 132's rule does not describe.

   probes/pfea.c enumerated every string over {a, '.', backslash, slash, colon} of length 0..8 --
   488281 of them -- and the narrow export matched change 132's wide rule with ZERO mismatches. Then
   it widened the alphabet by two characters, a space and 0xE9, and got 118587 mismatches out of
   960800. The smallest failing case is ". " -- a dot followed by a space.

   That is a rule nobody in this repository knew about, and the first question is not "what is it"
   but HOW FAR DOES IT REACH: change 132 converted PathFindExtensionW and validated it over 600k
   fuzz cases. If its corpus never put a space after a dot, the WIDE implementation already in this
   repository has the same gap, and that is a live bug in landed code rather than a note about a new
   target.

   So this probe asks both exports the same questions, side by side.                               */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char*    (WINAPI *FA)(const char*);
typedef wchar_t* (WINAPI *FW)(const wchar_t*);
static FA fa;
static FW fw;

static int model_132(const char* p){
    int end = 0; while (p[end]) ++end;
    for (int q = end; q > 0; ) {
        --q;
        if (p[q] == '.')  return q;
        if (p[q] == '\\') break;
    }
    return end;
}

static void both(const char* s){
    wchar_t w[64];
    int n = (int)strlen(s);
    for (int i = 0; i <= n; ++i) w[i] = (wchar_t)(unsigned char)s[i];
    int ra = (int)(fa(s) - s);
    int rw = (int)(fw(w) - w);
    int m  = model_132(s);
    printf("  \"%-10s\"  A=%-3d W=%-3d  132-model=%-3d  %s%s\n",
           s, ra, rw, m,
           (ra == rw) ? "" : "  <- A and W DISAGREE",
           (ra == m)  ? "" : "  <- model wrong");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    fa = (FA)GetProcAddress(hs, "PathFindExtensionA");
    fw = (FW)GetProcAddress(hs, "PathFindExtensionW");
    if (!fa || !fw) { printf("cannot resolve both exports\n"); return 1; }

    printf("=== the smallest failing cases, A and W side by side ===\n");
    both(". ");
    both(".a");
    both(". a");
    both("a. ");
    both("a.b ");
    both("a.b  ");
    both(" a.b");
    both("a .b");
    both("a.b c");
    both("file.txt ");
    both("file. txt");
    both("file .txt");

    printf("\n=== is it the SPACE specifically, or any character of some class? ===\n");
    printf("For each byte value, the string \"a.\" followed by that byte: does the dot still count?\n");
    {
        int special[300]; int nspecial = 0;
        for (int b = 1; b < 256; ++b) {
            char t[8];
            t[0]='a'; t[1]='.'; t[2]=(char)b; t[3]=0;
            int ra = (int)(fa(t) - t);
            if (ra != 1) { if (nspecial < 40) special[nspecial] = b; ++nspecial; }
        }
        printf("  %d of 255 byte values, placed AFTER the dot, stop it counting:", nspecial);
        for (int i = 0; i < nspecial && i < 40; ++i) printf(" %02X", special[i]);
        printf("\n");
    }
    {
        int special[300]; int nspecial = 0;
        for (int b = 1; b < 256; ++b) {
            char t[8];
            t[0]=(char)b; t[1]='.'; t[2]='x'; t[3]=0;
            int ra = (int)(fa(t) - t);
            if (ra != 1) { if (nspecial < 40) special[nspecial] = b; ++nspecial; }
        }
        printf("  %d of 255 byte values, placed BEFORE the dot, stop it counting:", nspecial);
        for (int i = 0; i < nspecial && i < 40; ++i) printf(" %02X", special[i]);
        printf("\n");
    }

    printf("\n=== how many trailing spaces does it take, and do they have to be trailing? ===\n");
    both("a.b");
    both("a.b ");
    both("a.b\t");
    both("a.b\n");
    both("a.b.");
    both("a.b. ");
    both("a. b");
    both("a.  b");

    printf("\n=== DOES THE WIDE FORM AGREE? (change 132 is already landed) ===\n");
    printf("Every string over {a, '.', backslash, space} of length 0..8, both exports.\n");
    {
        static const char AL[4] = { 'a', '.', '\\', ' ' };
        char s[12]; wchar_t w[12];
        long total = 0, aw_differ = 0, a_vs_model = 0, w_vs_model = 0;
        for (int len = 0; len <= 8; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v & 3]; w[i] = (wchar_t)s[i]; v >>= 2; }
                s[len] = 0; w[len] = 0;
                int ra = (int)(fa(s) - s);
                int rw = (int)(fw(w) - w);
                int m  = model_132(s);
                if (ra != rw) { if (++aw_differ <= 6)
                    printf("  A/W DIFFER on \"%s\": A=%d W=%d\n", s, ra, rw); }
                if (ra != m) ++a_vs_model;
                if (rw != m) ++w_vs_model;
                ++total;
            }
        }
        printf("\n  strings tested                          : %ld\n", total);
        printf("  A and W disagree with EACH OTHER on      : %ld\n", aw_differ);
        printf("  A disagrees with change 132's rule on    : %ld\n", a_vs_model);
        printf("  W disagrees with change 132's rule on    : %ld\n", w_vs_model);
        printf("\n  => %s\n", (w_vs_model > 0)
               ? "CHANGE 132 (ALREADY LANDED) HAS THE SAME GAP -- its rule is incomplete."
               : "the wide form matches 132's rule; only the NARROW one differs.");
    }
    return 0;
}
