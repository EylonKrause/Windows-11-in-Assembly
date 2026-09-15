/* changes/218-strtrima/probes/trim.c
   Pin down shlwapi!StrTrimA before writing any assembly.

   Why: 44622.19 ns to trim a 4000-character string against 7046.65 ns for StrTrimW over the same
   character count -- 6.33x the wide cost for HALF the bytes. Change 139 converted the wide form at
   20.9x geomean, and changes 214-216 have just built the machinery this needs: a 256-bit membership
   bitmap in the caller's shadow space plus a two-table vpshufb test.

   BOOL StrTrimA(PSTR pszSource, PCSTR pszTrimChars)   -- in place, and it MOVES the string.

   What has to be settled:
     * IS THE WALK BYTE-WISE ON THIS CODE PAGE? Asked separately from its siblings, because it is a
       different function. 6.33x is the MBCS-walk signature.
     * what exactly is trimmed -- leading only, trailing only, or both;
     * WHAT THE RETURN VALUE MEANS. "TRUE if anything was trimmed" is the obvious guess and obvious
       guesses are what this project keeps getting punished for;
     * a string made ENTIRELY of trim characters -- does it become empty;
     * the empty set and the empty source, and NULL for either;
     * whether the trailing scan and the leading scan use the SAME set (they need not);
     * and the one that costs the most to get wrong: DOES IT WRITE WHEN IT TRIMS NOTHING? An
       implementation that always re-terminates would touch a buffer the shipped one leaves alone,
       which no return-value comparison would ever catch.                                          */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *FN)(char*, const char*);
static FN trim;

#define POISON '#'

static void show(const char* src, const char* set, const char* tag){
    char b[64];
    int n = (int)strlen(src);
    memset(b, POISON, sizeof(b));
    memcpy(b, src, (size_t)n + 1);
    BOOL r = trim(b, set);
    printf("  %-22s set=%-8s -> ret=%-5s result=\"%s\"  tail=",
           tag, set[0] ? set : "\"\"", r ? "TRUE" : "FALSE", b);
    /* show what the bytes AFTER the new terminator look like, to see how much was rewritten */
    int rl = (int)strlen(b);
    for (int i = rl; i < n + 2 && i < 40; ++i)
        putchar(b[i] == POISON ? '-' : (b[i] == 0 ? '.' : b[i]));
    printf("\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    trim = (FN)GetProcAddress(hs, "StrTrimA");
    if (!trim) { printf("no StrTrimA\n"); return 1; }
    printf("StrTrimA = %p\nGetACP() = %u\n\n", (void*)trim, GetACP());

    printf("=== THE DECIDING TEST: is the walk byte-wise on this code page? ===\n");
    {
        int bad = 0;
        for (int b = 1; b < 256; ++b) {
            char t[16];
            if (b == 'x') continue;
            /* "xx<byte>" trimmed of 'x' should leave "<byte>" if the walk is byte-wise */
            t[0]='x'; t[1]='x'; t[2]=(char)b; t[3]='y'; t[4]=0;
            trim(t, "x");
            if (!(t[0] == (char)b && t[1] == 'y' && t[2] == 0)) {
                if (bad < 8) printf("  byte %02X after the trimmed prefix -> \"%s\"\n", b, t);
                ++bad;
            }
        }
        printf("  %d of 254 byte values misbehave.\n", bad);
        printf("  => %s\n\n", bad == 0
               ? "byte-wise: a 256-bit membership set reproduces it exactly"
               : "MBCS-AWARE: a byte-wise trim would DISAGREE -- target dead unless modelled");
    }

    printf("=== leading, trailing, or both? ===\n");
    show("xxabcxx", "x", "both ends");
    show("xxabc",   "x", "leading only");
    show("abcxx",   "x", "trailing only");
    show("abc",     "x", "nothing to trim");
    show("xxxxx",   "x", "ENTIRELY trim chars");
    show("x",       "x", "single trim char");
    show("",        "x", "empty source");
    show("abc",     "",  "EMPTY set");
    show("  abc  ", " ", "spaces, the usual use");
    show("xyxabcyx","xy","two-character set");
    show("abcxabc", "x", "trim char in the MIDDLE only");

    printf("\n=== WHAT DOES THE RETURN VALUE MEAN? ===\n");
    printf("(TRUE on \"something was trimmed\" is the obvious guess. Checked, not assumed.)\n");
    {
        struct { const char* s; const char* set; } C[] = {
            {"xxabc","x"}, {"abcxx","x"}, {"abc","x"}, {"xxxxx","x"}, {"","x"}, {"abc",""},
            {"x","x"}, {"ax","x"}, {"xa","x"},
        };
        for (int i = 0; i < 9; ++i) {
            char b[32];
            strcpy(b, C[i].s);
            BOOL r = trim(b, C[i].set);
            printf("  %-8s set=%-4s -> %-5s  result=\"%s\"\n",
                   C[i].s[0] ? C[i].s : "\"\"", C[i].set[0] ? C[i].set : "\"\"",
                   r ? "TRUE" : "FALSE", b);
        }
    }

    printf("\n=== DOES IT WRITE WHEN IT TRIMS NOTHING? ===\n");
    printf("An implementation that always re-terminates would touch bytes the shipped one leaves\n");
    printf("alone, and NO return-value comparison would ever catch it. The bytes after the\n");
    printf("terminator are poisoned and inspected.\n");
    {
        char b[32];
        memset(b, POISON, sizeof(b));
        memcpy(b, "abc", 4);
        trim(b, "x");
        printf("  \"abc\" trimmed of 'x': bytes 0..7 = ");
        for (int i = 0; i < 8; ++i) printf("%s", b[i]==POISON?"-":(b[i]==0?".":"x"));
        printf("   (\"xxx.----\" = it only kept the existing terminator)\n");

        memset(b, POISON, sizeof(b));
        memcpy(b, "abcxx", 6);
        trim(b, "x");
        printf("  \"abcxx\" trimmed:      bytes 0..7 = ");
        for (int i = 0; i < 8; ++i) printf("%s", b[i]==POISON?"-":(b[i]==0?".":"x"));
        printf("   (does it clear the old tail or just terminate?)\n");

        memset(b, POISON, sizeof(b));
        memcpy(b, "xxabc", 6);
        trim(b, "x");
        printf("  \"xxabc\" trimmed:      bytes 0..7 = ");
        for (int i = 0; i < 8; ++i) printf("%s", b[i]==POISON?"-":(b[i]==0?".":"x"));
        printf("   (after moving 'abc' down, what is left at 3..5?)\n");
    }

    printf("\n=== NULL arguments ===\n");
    {
        char b[32]; strcpy(b, "xxabcxx");
        printf("  StrTrimA(buf, NULL) = "); fflush(stdout);
        BOOL r = trim(b, NULL);
        printf("%s, buffer=\"%s\"\n", r ? "TRUE" : "FALSE", b);
        printf("  StrTrimA(NULL, \"x\") = "); fflush(stdout);
        r = trim(NULL, "x");
        printf("%s\n", r ? "TRUE" : "FALSE");
    }

    printf("\n=== every byte value as a trim character, leading and trailing ===\n");
    {
        int bad = 0;
        for (int b = 1; b < 256; ++b) {
            char t[16], set[4];
            char f = (char)((b == 0x41) ? 0x42 : 0x41);
            set[0] = (char)b; set[1] = 0;
            t[0]=(char)b; t[1]=(char)b; t[2]=f; t[3]=(char)b; t[4]=0;
            trim(t, set);
            if (!(t[0] == f && t[1] == 0)) { if (bad < 8) printf("  trim char %02X -> \"%s\"\n", b, t); ++bad; }
        }
        printf("  %d of 255 byte values fail to trim from both ends\n", bad);
    }
    return 0;
}
