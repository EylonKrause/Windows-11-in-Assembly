/* changes/220-strchra/probes/chr.c
   Pin down shlwapi!StrChrA before writing any assembly.

   This one is a DIFFERENT SHAPE from the rest of the narrow family, and the numbers say so.
   StrRChrA cost 16.41x its wide sibling, StrCSpnA 13.54x, StrPBrkA 10.03x -- all the signature of an
   MBCS-aware walk with a call per character. StrChrA costs 1580.19 ns against StrChrW's 1565.97 over
   the same character count: 1.01x the wide cost for HALF the bytes, i.e. only twice the cost per
   byte. That is a plain byte loop, not a CharNextA walk.

   So the available ratio is smaller and comes from vectorisation alone. Change 131 converted
   StrChrW at 7.14x geomean; a narrow block carries 32 characters to the wide form's 16, so this
   should land near twice that -- which is worth having, but it is not a 200x target and should not
   be described as one.

   PSTR StrChrA(PCSTR pszStart, WORD wMatch)

   What has to be settled:
     * Is it byte-wise? Asked separately from its siblings, because it is a different function and,
       by the timing, a differently implemented one.
     * wMatch is a WORD: is only the low byte consulted, as it is for StrRChrA?
     * searching for the TERMINATOR;
     * NULL, and the empty string;
     * and whether it can find a byte in the HIGH half, 0x80..0xFF, which are ordinary characters on
       code page 1252.                                                                             */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char* (WINAPI *FN)(const char*, WORD);
static FN chr;

static void show(const char* s, unsigned m, const char* tag){
    char* r;
    printf("  %-34s -> ", tag);
    fflush(stdout);
    r = chr(s, (WORD)m);
    if (r) printf("offset %d\n", (int)(r - s)); else printf("NULL\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    chr = (FN)GetProcAddress(hs, "StrChrA");
    if (!chr) { printf("no StrChrA\n"); return 1; }
    printf("StrChrA = %p\nGetACP() = %u\n\n", (void*)chr, GetACP());

    printf("=== THE DECIDING TEST: is the walk byte-wise on this code page? ===\n");
    {
        int bad = 0;
        for (int b = 1; b < 256; ++b) {
            char t[8];
            if (b == 'Z') continue;
            t[0]='a'; t[1]=(char)b; t[2]='Z'; t[3]='q'; t[4]=0;
            char* r = chr(t, 'Z');
            if (!r || (r - t) != 2) {
                if (bad < 8) printf("  byte %02X before the target -> %s%d\n",
                                    b, r ? "offset " : "NULL", r ? (int)(r-t) : -1);
                ++bad;
            }
        }
        printf("  %d of 254 byte values behave as a LEAD BYTE here.\n", bad);
        printf("  => %s\n\n", bad == 0 ? "byte-wise: a vector scan reproduces it exactly"
                                       : "MBCS-AWARE: a byte scan would DISAGREE");
    }

    printf("=== the match value is a WORD; which bytes of it count? ===\n");
    {
        static const char s[] = "abcZdefZghi";   /* Z at 3 and 7 */
        show(s, 'Z',    "wMatch = 0x005A ('Z')");
        show(s, 0x015A, "wMatch = 0x015A (low byte 'Z')");
        show(s, 0x5A5A, "wMatch = 0x5A5A (low byte 'Z')");
        show(s, 0xFF5A, "wMatch = 0xFF5A (low byte 'Z')");
        show(s, 0x5A00, "wMatch = 0x5A00 (low byte NUL)");
        show(s, 'Q',    "absent");
    }

    printf("\n=== the terminator, the empty string, and NULL ===\n");
    {
        static const char s[] = "abc";
        static const char e[] = "";
        show(s, 0,   "searching \"abc\" for NUL");
        show(e, 'a', "empty string, absent");
        show(e, 0,   "empty string, searching for NUL");
        printf("  %-34s -> ", "StrChrA(NULL, 'a')"); fflush(stdout);
        { char* r = chr(NULL, 'a'); printf("%s\n", r ? "non-NULL" : "NULL"); }
    }

    printf("\n=== every byte value as the TARGET, including the high half ===\n");
    printf("(0x80..0xFF are ordinary characters in code page 1252 and must be findable.)\n");
    {
        int bad = 0;
        for (int b = 1; b < 256; ++b) {
            char t[8];
            char f = (char)((b == 0xF1) ? 0xF2 : 0xF1);
            t[0]=f; t[1]=(char)b; t[2]=f; t[3]=(char)b; t[4]=0;
            char* r = chr(t, (WORD)b);
            if (!r || (r - t) != 1) { if (bad < 8) printf("  target %02X -> %s%d (expected 1)\n",
                                        b, r ? "offset " : "NULL", r ? (int)(r-t) : -1); ++bad; }
        }
        printf("  %d of 255 byte values are not found at the FIRST occurrence\n", bad);
    }

    printf("\n=== does it stop at the terminator? ===\n");
    {
        static char t[16];
        memcpy(t, "abc\0Zxy\0", 8);
        printf("  %-34s -> ", "\"abc\\0Zxy\\0\" searched for 'Z'"); fflush(stdout);
        { char* r = chr(t, 'Z'); printf("%s%d  (NULL = it stopped at the embedded NUL)\n",
                                        r ? "offset " : "NULL ", r ? (int)(r - t) : -1); }
    }

    printf("\n=== a long string, to confirm there is no length cap ===\n");
    {
        static char big[5000];
        for (int i = 0; i < 4000; ++i) big[i] = 'a';
        big[3500] = 'Z'; big[4000] = 0;
        char* r = chr(big, 'Z');
        printf("  4000 characters, target at 3500 -> %s%d\n",
               r ? "offset " : "NULL ", r ? (int)(r - big) : -1);
    }
    return 0;
}
