/* changes/221-pathremoveblanksa/probes/blanks.c
   Pin down shlwapi!PathRemoveBlanksA before writing any assembly.

   Why: discovery/shlwapi_narrow2.c timed the twelve narrow shlwapi siblings still unconverted, and
   this one carries the biggest ratio by a distance -- 158.64 ns against 19.86 ns for
   PathRemoveBlanksW on the same character count, 7.99x the wide cost for HALF the bytes. Change 141
   converted the wide form at 4.68x geomean.

   void PathRemoveBlanksA(PSTR pszPath)   -- in place, and it MOVES the string.

   What has to be settled:
     * What counts as a blank? "Blanks" could mean 0x20 alone, or whitespace generally. Every byte
       value gets asked, at the front and at the back, rather than assuming either.
     * leading only, trailing only, or both;
     * a string made entirely of blanks;
     * the empty string and NULL;
     * blanks in the MIDDLE, which must survive;
     * and what it writes. Change 218 established for StrTrimA that the export writes only what it
       must and that the ORDER of its writes is observable -- trimming both ends leaves TWO
       terminators behind. The same question is asked here, with the buffer poisoned and read back,
       because no return-value comparison can see it and this function returns nothing at all.

   AND THE STRONGER BYTE-WISE SCREEN. StrStrA passed the usual "put a byte in front" test and was
   still not byte-wise: its comparison conflated two byte values INSIDE a candidate. So the screen
   here varies the byte the function actually examines -- every value, in every position that
   matters -- rather than a byte beside it.                                                        */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void (WINAPI *FN)(char*);
static FN rb;

#define POISON '#'

static void show(const char* src, const char* tag){
    char b[64];
    int n = (int)strlen(src);
    memset(b, POISON, sizeof(b));
    memcpy(b, src, (size_t)n + 1);
    rb(b);
    printf("  %-26s -> \"%s\"   buffer=", tag, b);
    for (int i = 0; i < n + 3 && i < 40; ++i)
        putchar(b[i] == POISON ? '-' : (b[i] == 0 ? '.' : b[i]));
    printf("\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    rb = (FN)GetProcAddress(hs, "PathRemoveBlanksA");
    if (!rb) { printf("no PathRemoveBlanksA\n"); return 1; }
    printf("PathRemoveBlanksA = %p\nGetACP() = %u\n\n", (void*)rb, GetACP());

    printf("=== WHAT COUNTS AS A BLANK? every byte value, leading and trailing ===\n");
    {
        int lead[300], nlead = 0, trail[300], ntrail = 0;
        for (int b = 1; b < 256; ++b) {
            char t[16];
            /* leading: "<b>ab" -- if b is a blank the result is "ab" */
            t[0]=(char)b; t[1]='a'; t[2]='b'; t[3]=0;
            rb(t);
            if (strlen(t) == 2 && t[0]=='a') { if (nlead < 300) lead[nlead++] = b; }
            /* trailing: "ab<b>" -- if b is a blank the result is "ab" */
            t[0]='a'; t[1]='b'; t[2]=(char)b; t[3]=0;
            rb(t);
            if (strlen(t) == 2) { if (ntrail < 300) trail[ntrail++] = b; }
        }
        printf("  LEADING blanks  (%d):", nlead);
        for (int i = 0; i < nlead && i < 20; ++i) printf(" %02X", lead[i]);
        printf("\n  TRAILING blanks (%d):", ntrail);
        for (int i = 0; i < ntrail && i < 20; ++i) printf(" %02X", trail[i]);
        printf("\n  => %s\n\n",
               (nlead == ntrail && nlead <= 4) ? "a small, fixed set -- reproducible"
                                               : "check this carefully before implementing");
    }

    printf("=== leading, trailing, or both? and what survives in the middle ===\n");
    show("  abc  ", "spaces both ends");
    show("  abc",   "leading only");
    show("abc  ",   "trailing only");
    show("abc",     "nothing to remove");
    show("     ",   "ENTIRELY blanks");
    show(" ",       "a single blank");
    show("",        "empty string");
    show("a b c",   "blanks in the MIDDLE");
    show("  a b  ", "both, with a middle blank");
    show("\ta\t",   "tabs");
    show("C:\\Program Files\\x  ", "a real path with a trailing blank");

    printf("\n=== WHAT DOES IT WRITE? (the question change 218 had to answer for StrTrimA) ===\n");
    printf("The buffer is poisoned past the terminator and read back. An implementation that\n");
    printf("re-terminated unconditionally, or cleared the vacated tail, would leave the same STRING\n");
    printf("and this function returns NOTHING, so only the buffer can tell them apart.\n");
    {
        char b[32];
        memset(b, POISON, sizeof(b));
        memcpy(b, "abc", 4);
        rb(b);
        printf("  \"abc\" (nothing to do): ");
        for (int i = 0; i < 8; ++i) putchar(b[i]==POISON?'-':(b[i]==0?'.':b[i]));
        printf("   (\"abc.----\" = the buffer was not touched at all)\n");

        memset(b, POISON, sizeof(b));
        memcpy(b, "abc  ", 6);
        rb(b);
        printf("  \"abc  \" (trailing):   ");
        for (int i = 0; i < 8; ++i) putchar(b[i]==POISON?'-':(b[i]==0?'.':b[i]));
        printf("\n");

        memset(b, POISON, sizeof(b));
        memcpy(b, "  abc", 6);
        rb(b);
        printf("  \"  abc\" (leading):    ");
        for (int i = 0; i < 8; ++i) putchar(b[i]==POISON?'-':(b[i]==0?'.':b[i]));
        printf("\n");

        memset(b, POISON, sizeof(b));
        memcpy(b, "  abc  ", 8);
        rb(b);
        printf("  \"  abc  \" (both):     ");
        for (int i = 0; i < 10; ++i) putchar(b[i]==POISON?'-':(b[i]==0?'.':b[i]));
        printf("   (two terminators would mean the trailing cut happens FIRST)\n");
    }

    printf("\n=== NULL ===\n");
    {
        printf("  PathRemoveBlanksA(NULL) ... "); fflush(stdout);
        rb(NULL);
        printf("returned without faulting\n");
    }

    printf("\n=== THE STRONGER BYTE-WISE SCREEN ===\n");
    printf("StrStrA passed the usual \"put a byte in front\" test and was still not byte-wise. So\n");
    printf("every byte value is varied where the function actually looks: immediately inside the\n");
    printf("leading run and immediately inside the trailing run.\n");
    {
        int odd = 0;
        for (int b = 1; b < 256; ++b) {
            if (b == ' ') continue;
            char t[16];
            /* "  <b>x  " -- the leading run must stop AT b, whatever b is */
            t[0]=' '; t[1]=' '; t[2]=(char)b; t[3]='x'; t[4]=' '; t[5]=' '; t[6]=0;
            rb(t);
            if (!(strlen(t) == 2 && (unsigned char)t[0] == (unsigned char)b && t[1] == 'x')) {
                if (odd < 10) printf("  byte %02X inside the run -> \"%s\" (len %d)\n",
                                     b, t, (int)strlen(t));
                ++odd;
            }
        }
        printf("  %d of 254 byte values behave unexpectedly INSIDE the string\n", odd);
        printf("  => %s\n", odd == 0 ? "byte-wise where it matters: a vector scan reproduces it"
                                     : "NOT byte-wise -- sweep the equivalence before implementing");
    }

    printf("\n=== a long path, to confirm there is no length cap ===\n");
    {
        static char big[1200];
        for (int i = 0; i < 1100; ++i) big[i] = (char)('a' + i % 26);
        big[0] = ' '; big[1] = ' '; big[1098] = ' '; big[1099] = ' ';
        big[1100] = 0;
        rb(big);
        printf("  1100 characters, two blanks at each end -> new length %d (expect 1096)\n",
               (int)strlen(big));
    }
    return 0;
}
