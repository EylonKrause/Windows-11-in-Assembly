/* changes/217-pathfindextensiona/probes/pfea.c
   Pin down shlwapi!PathFindExtensionA before writing any assembly.

   Why: 183.00 ns for a 55-character path against 37.64 ns for PathFindExtensionW on the SAME path --
   4.86x the wide cost for HALF the bytes, the MBCS-walk signature the whole narrow shlwapi family
   has shown. Change 132 already converted the wide form.

   The wide rule, as change 132 derived and validated it over 600k fuzz cases, is genuinely
   surprising and is the thing that must NOT be assumed here:

       the extension is the LAST '.' that occurs after the last BACKSLASH. Only '\' terminates the
       search -- '/' and ':' do NOT, even though PathFindFileNameW treats both as separators. So
       "a.b/c" gives the '.' at index 1, while "a.b\c" gives the terminator.

   That asymmetry between the two Path functions is exactly the kind of thing an A form could get
   differently, and change 212 has already shown how this goes wrong: every spot check there agreed
   with a rule that was wrong on 76672 enumerated strings. So this probe does not spot-check. It
   ENUMERATES every string over {a, '.', '\', '/', ':'} up to length 8 -- 488281 of them -- and
   compares the live NARROW export against the wide rule.

   Plus the usual: is the walk byte-wise on this code page, what happens with NULL, and where the
   returned pointer lands when there is no extension.                                             */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char* (WINAPI *FN)(const char*);
static FN fext;

/* the rule change 132 derived for the WIDE form, transcribed to bytes */
static int model_132(const char* p){
    int end = 0; while (p[end]) ++end;
    for (int q = end; q > 0; ) {
        --q;
        if (p[q] == '.')  return q;
        if (p[q] == '\\') break;
    }
    return end;
}

/* the plausible alternative: '/' and ':' stop the search too, as they do in PathFindFileName */
static int model_allseps(const char* p){
    int end = 0; while (p[end]) ++end;
    for (int q = end; q > 0; ) {
        --q;
        if (p[q] == '.') return q;
        if (p[q] == '\\' || p[q] == '/' || p[q] == ':') break;
    }
    return end;
}

static void show(const char* p){
    char* r = fext(p);
    printf("  %-24s -> offset %-3d  %s\n", p[0] ? p : "(empty)", (int)(r - p),
           *r ? r : "(the terminator)");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    fext = (FN)GetProcAddress(hs, "PathFindExtensionA");
    if (!fext) { printf("no PathFindExtensionA\n"); return 1; }
    printf("PathFindExtensionA = %p\nGetACP() = %u\n\n", (void*)fext, GetACP());

    printf("=== THE DECIDING TEST: is the walk byte-wise on this code page? ===\n");
    {
        int bad = 0;
        for (int b = 1; b < 256; ++b) {
            char t[10];
            if (b == '.' || b == '\\') continue;          /* would change the expected answer */
            /* a, <byte>, '.', x  -- a byte scan puts the extension at index 2 */
            t[0]='a'; t[1]=(char)b; t[2]='.'; t[3]='x'; t[4]=0;
            char* r = fext(t);
            if ((r - t) != 2) { if (bad < 8) printf("  byte %02X before the dot -> %d (expected 2)\n",
                                                    b, (int)(r - t)); ++bad; }
        }
        printf("  %d of 254 byte values behave as a LEAD BYTE here.\n", bad);
        printf("  => %s\n\n", bad == 0
               ? "byte-wise: a vector scan can reproduce this exactly"
               : "MBCS-AWARE: a plain byte scan would DISAGREE -- target dead unless modelled");
    }

    printf("=== the surprising part of the wide rule, spot-checked first ===\n");
    printf("(change 132: only BACKSLASH stops the search -- '/' and ':' do NOT, unlike\n");
    printf(" PathFindFileName, which treats all three as separators.)\n");
    show("file.txt");
    show("a.b/c");
    show("a.b\\c");
    show("a.b:c");
    show("C:\\dir\\file.tar.gz");
    show("C:\\dir.x\\file");
    show(".hidden");
    show("a.b.");
    show("noext");
    show("");
    show(".");
    show("..");
    {
        char* r = fext(NULL);
        printf("  %-24s -> %s\n", "NULL", r ? "non-NULL" : "NULL");
    }

    printf("\n=== AND NOW THE ENUMERATION, because spot checks are how 212 nearly went wrong ===\n");
    printf("Every string over {a, '.', backslash, slash, colon} of length 0..8.\n");
    {
        static const char AL[5] = { 'a', '.', '\\', '/', ':' };
        char s[12];
        long total = 0, bad132 = 0, badall = 0;
        for (int len = 0; len <= 8; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 5;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 5]; v /= 5; }
                s[len] = 0;
                int live = (int)(fext(s) - s);
                if (model_132(s)     != live) { if (++bad132 <= 6)
                    printf("  132-rule MISMATCH  \"%s\" live=%d model=%d\n", s, live, model_132(s)); }
                if (model_allseps(s) != live) { if (++badall <= 4)
                    printf("  all-separators rule MISMATCH \"%s\" live=%d model=%d\n",
                           s, live, model_allseps(s)); }
                ++total;
            }
        }
        printf("\n  strings tested                         : %ld\n", total);
        printf("  mismatches vs the 132 (wide) rule      : %ld\n", bad132);
        printf("  mismatches vs \"all three separators\"   : %ld\n", badall);
        printf("\n  => %s\n", bad132 == 0
               ? "THE NARROW FORM CARRIES THE WIDE FORM'S RULE EXACTLY, backslash-only included."
               : "the narrow form does NOT match the wide rule -- derive it separately");
        if (badall && !bad132)
            printf("  => and the backslash-only asymmetry is REAL: treating '/' and ':' as stops too\n"
                   "     (as PathFindFileName does) is wrong on %ld of these.\n", badall);
    }

    printf("\n=== a wider alphabet, to be sure nothing else is special ===\n");
    {
        static const char A2[7] = { 'a', '.', '\\', '/', ':', ' ', (char)0xE9 };
        char s[12];
        long t2 = 0, b2 = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 7;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = A2[v % 7]; v /= 7; }
                s[len] = 0;
                if (model_132(s) != (int)(fext(s) - s)) {
                    if (++b2 <= 6) printf("  MISMATCH \"%s\"\n", s);
                }
                ++t2;
            }
        }
        printf("  %ld strings over {a,.,backslash,slash,colon,space,0xE9}: %ld mismatches\n", t2, b2);
    }
    return 0;
}
