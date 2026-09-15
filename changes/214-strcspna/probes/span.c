/* changes/214-strcspna/probes/span.c
   Pin down the narrow shlwapi SPAN family before writing any assembly.

   Three exports share one job -- classify each character by membership in a set -- and the narrow
   survey found all three carrying large ratios against their wide siblings, which this project has
   already converted:

     export      A, 4000 chars     W, 4000 chars    A / W    W change   its geomean
     StrCSpnA      42868.10 ns        3166.56 ns    13.54x   136        12.289x
     StrPBrkA      23808.12 ns        2374.10 ns    10.03x   137         9.325x
     StrSpnA      166503.08 ns       22141.23 ns     7.52x   135         3.095x

   StrCSpnA therefore carries the most available speedup of the three, and it is probed first.

   A 256-bit membership set over BYTES is a far better fit than anything the wide forms can use: the
   whole set fits in a 16x16 bit matrix, and testing 32 characters against it costs a handful of
   vpshufb-class instructions. But none of that matters until the contract is settled:

     * IS THE WALK BYTE-WISE ON THIS CODE PAGE? Ten to fourteen times the wide cost is the signature
       of an MBCS-aware walk. If some byte acts as a lead byte the target is dead. Asked separately
       for each of the three, because they are three different functions.
     * IS THE SET STRING ALSO WALKED MBCS-STYLE? It is a string too, and it could be classified
       differently from the subject.
     * the empty set, the empty subject, NULL arguments, and a set containing duplicates;
     * what each one returns when nothing matches, which is where span and complement-span differ;
     * and whether a high byte 0x80..0xFF can be a set member at all.                               */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int   (WINAPI *FN_SPN)(const char*, const char*);
typedef char* (WINAPI *FN_BRK)(const char*, const char*);
static FN_SPN spn, cspn;
static FN_BRK brk;

static void row(const char* s, const char* set){
    int a = spn ? spn(s, set) : -1;
    int b = cspn ? cspn(s, set) : -1;
    char* c = brk ? brk(s, set) : NULL;
    printf("  s=%-14s set=%-10s  StrSpnA=%-4d StrCSpnA=%-4d StrPBrkA=%s",
           s[0] ? s : "\"\"", set[0] ? set : "\"\"", a, b, c ? "offset " : "NULL");
    if (c) printf("%d", (int)(c - s));
    printf("\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    spn  = (FN_SPN)GetProcAddress(hs, "StrSpnA");
    cspn = (FN_SPN)GetProcAddress(hs, "StrCSpnA");
    brk  = (FN_BRK)GetProcAddress(hs, "StrPBrkA");
    if (!spn || !cspn || !brk) { printf("cannot resolve the span family\n"); return 1; }
    printf("StrSpnA=%p StrCSpnA=%p StrPBrkA=%p\nGetACP() = %u\n\n",
           (void*)spn, (void*)cspn, (void*)brk, GetACP());

    printf("=== THE DECIDING TEST: is the SUBJECT walked byte-wise? ===\n");
    printf("An MBCS walk that treated some byte as a lead byte would swallow the character after it,\n");
    printf("so a set member sitting there would be invisible to the export and visible to a byte\n");
    printf("scan. Every byte value 0x01..0xFF is tried in that position, for all three exports.\n");
    {
        int bad_c = 0, bad_b = 0, bad_s = 0;
        for (int b = 1; b < 256; ++b) {
            char t[8];
            /* subject: a, <byte>, Z, q   with the set {Z}. A byte scan says the Z is at index 2. */
            t[0]='a'; t[1]=(char)b; t[2]='Z'; t[3]='q'; t[4]=0;
            if (b == 'Z') continue;                     /* would place a second set member */
            if (cspn(t, "Z") != 2) { if (bad_c < 6) printf("  StrCSpnA: byte %02X -> %d (expected 2)\n", b, cspn(t,"Z")); ++bad_c; }
            { char* r = brk(t, "Z");
              if (!r || (r - t) != 2) { if (bad_b < 6) printf("  StrPBrkA: byte %02X -> %d\n", b, r?(int)(r-t):-1); ++bad_b; } }
            /* subject: a, <byte>, Z, q  with the set {a, <byte>}: the span should be 2 */
            { char set2[4]; set2[0]='a'; set2[1]=(char)b; set2[2]=0;
              if (spn(t, set2) != 2) { if (bad_s < 6) printf("  StrSpnA: byte %02X -> %d (expected 2)\n", b, spn(t,set2)); ++bad_s; } }
        }
        printf("  StrCSpnA: %d of 254 byte values misbehave\n", bad_c);
        printf("  StrPBrkA: %d of 254 byte values misbehave\n", bad_b);
        printf("  StrSpnA : %d of 254 byte values misbehave\n", bad_s);
        printf("  => %s\n\n", (bad_c==0 && bad_b==0 && bad_s==0)
               ? "all three are byte-wise: a 256-bit membership set reproduces them exactly"
               : "at least one is MBCS-AWARE -- that one is dead unless modelled");
    }

    printf("=== is the SET string also byte-wise? (a high byte must be able to BE a member) ===\n");
    {
        int bad = 0;
        for (int b = 1; b < 256; ++b) {
            char t[8], set[4];
            t[0]='a'; t[1]='b'; t[2]=(char)b; t[3]='q'; t[4]=0;
            if (b=='a'||b=='b'||b=='q') continue;
            set[0]=(char)b; set[1]=0;
            if (cspn(t, set) != 2) { if (bad < 6) printf("  set byte %02X -> %d (expected 2)\n", b, cspn(t,set)); ++bad; }
        }
        printf("  %d of 252 byte values cannot be a set member\n", bad);
        /* and a two-byte set whose FIRST byte is a would-be lead byte */
        {
            int bad2 = 0;
            for (int b = 1; b < 256; ++b) {
                char t[8], set[4];
                if (b=='q'||b=='Z') continue;
                t[0]='a'; t[1]='b'; t[2]='Z'; t[3]='q'; t[4]=0;
                set[0]=(char)b; set[1]='Z'; set[2]=0;   /* if b were a lead byte, 'Z' would vanish */
                if (cspn(t, set) != 2) { if (bad2 < 6) printf("  set \"%02X Z\" -> %d (expected 2)\n", b, cspn(t,set)); ++bad2; }
            }
            printf("  %d of 254 byte values hide a FOLLOWING set member\n", bad2);
        }
    }

    printf("\n=== the degenerate cases ===\n");
    row("abcdef", "abc");
    row("abcdef", "def");
    row("abcdef", "xyz");
    row("abcdef", "abcdef");
    row("abcdef", "");
    row("", "abc");
    row("", "");
    row("aaa", "a");
    row("abcabc", "cba");
    row("abcdef", "aabbcc");        /* duplicates in the set */

    printf("\n=== NULL arguments ===\n");
    {
        printf("  StrCSpnA(NULL, \"abc\") = "); fflush(stdout);
        printf("%d\n", cspn(NULL, "abc"));
        printf("  StrCSpnA(\"abc\", NULL) = "); fflush(stdout);
        printf("%d\n", cspn("abc", NULL));
        printf("  StrSpnA (NULL, \"abc\") = "); fflush(stdout);
        printf("%d\n", spn(NULL, "abc"));
        printf("  StrSpnA (\"abc\", NULL) = "); fflush(stdout);
        printf("%d\n", spn("abc", NULL));
        printf("  StrPBrkA(NULL, \"abc\") = "); fflush(stdout);
        { char* r = brk(NULL, "abc"); printf("%s\n", r ? "non-NULL" : "NULL"); }
        printf("  StrPBrkA(\"abc\", NULL) = "); fflush(stdout);
        { char* r = brk("abc", NULL); printf("%s\n", r ? "non-NULL" : "NULL"); }
    }

    printf("\n=== does the TERMINATOR count as a set member if the set contains one? ===\n");
    printf("(It cannot be written into a C set string, but a set whose first byte is NUL is the\n");
    printf(" empty set -- the question is whether the subject's own terminator can ever match.)\n");
    row("abc", "");

    printf("\n=== a long subject, to confirm there is no length cap ===\n");
    {
        static char big[2048];
        for (int i = 0; i < 2000; ++i) big[i] = 'a';
        big[1500] = 'Z'; big[2000] = 0;
        printf("  2000 'a' with a 'Z' at 1500: StrCSpnA=%d  StrSpnA(set \"a\")=%d\n",
               cspn(big, "Z"), spn(big, "a"));
    }
    return 0;
}
