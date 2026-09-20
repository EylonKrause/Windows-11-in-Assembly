/* changes/213-strrchra/probes/srca.c
   Pin down shlwapi!StrRChrA before writing any assembly.

   Why: it carries the biggest ratio the narrow survey found. 14351.08 ns to search 4000 characters
   backwards, against 874.77 ns for StrRChrW over the same character count -- SIXTEEN TIMES the wide
   cost for HALF the bytes, i.e. thirty-two times the cost per byte. Change 134 already converted the
   wide form (4.80x geomean), so the technique is known; what is not known is the contract.

   PSTR StrRChrA(PCSTR pszStart, PCSTR pszEnd, WORD wMatch)

   What has to be settled before any of that matters:
     * Is the walk byte-wise on this code page? Sixteen times the wide cost is the signature of an
       MBCS-aware walk -- CharNextA per character. If some byte acts as a lead byte, a byte-wise
       reverse scan would disagree with the export and the target is dead, the way StrCmpNW and
       lstrcmpA died on being linguistic. (212 asked the same question of PathFindFileNameA and got
       zero of 255; this function is asked separately, because it is a different function.)
     * Is pszEnd inclusive or exclusive? The whole loop bound depends on it, and the answer is
       checked at the exact boundary, not inferred from a case in the middle.
     * pszEnd == NULL -- does it mean "to the end of the string"?
     * wMatch is a WORD on a code page with no lead bytes: is only the low byte consulted?
     * searching for the TERMINATOR, an empty string, NULL arguments, and an pszEnd that lies BEFORE
       pszStart -- the degenerate cases a reverse scan can get wrong in silence.                    */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char* (WINAPI *FN)(const char*, const char*, WORD);
static FN rchr;

/* The tag is printed BEFORE the call, not after. The first run of this probe hung inside the live
   export and printed nothing to say where, which cost a 300-second timeout to locate. With the tag
   out first, the last line on screen names the call that did not return. */
static void show(const char* s, int endoff, unsigned match, const char* tag){
    const char* end = (endoff < 0) ? NULL : s + endoff;
    char* r;
    printf("  %-38s -> ", tag);
    fflush(stdout);
    r = rchr(s, end, (WORD)match);
    if (r) printf("offset %d\n", (int)(r - s)); else printf("NULL\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    rchr = (FN)GetProcAddress(hs, "StrRChrA");
    if (!rchr) { printf("no StrRChrA\n"); return 1; }
    printf("StrRChrA = %p\nGetACP() = %u\n\n", (void*)rchr, GetACP());

    printf("=== THE DECIDING TEST: is the walk byte-wise on this code page? ===\n");
    printf("An MBCS walk that treated some byte as a lead byte would swallow the byte after it, so a\n");
    printf("target character sitting there would be invisible to the export and visible to a byte\n");
    printf("scan. Every byte value 0x01..0xFF is tried in that position.\n");
    {
        int suspicious = 0;
        for (int b = 1; b < 256; ++b) {
            char t[8];
            t[0]='a'; t[1]=(char)b; t[2]='Z'; t[3]='q'; t[4]=0;
            char* r = rchr(t, NULL, 'Z');
            if (!r || (r - t) != 2) {
                if (suspicious < 10)
                    printf("  byte %02X before the target -> %s%d (a byte scan says 2)\n",
                           b, r ? "offset " : "NULL", r ? (int)(r - t) : -1);
                ++suspicious;
            }
        }
        printf("  %d of 255 byte values behave as a LEAD BYTE here.\n", suspicious);
        printf("  => %s\n\n", suspicious == 0
               ? "byte-wise: a vector reverse scan can reproduce this exactly"
               : "MBCS-AWARE: a plain byte scan would DISAGREE -- target dead unless modelled");
    }

    printf("=== is pszEnd INCLUSIVE or EXCLUSIVE? (checked AT the boundary) ===\n");
    {
        static const char s[] = "abcZdefZghi";     /* Z at 3 and 7, length 11 */
        printf("  string \"%s\": Z at offsets 3 and 7, length %d\n", s, (int)strlen(s));
        show(s, -1, 'Z', "pszEnd = NULL");
        show(s, 11, 'Z', "pszEnd = s+11 (the terminator)");
        show(s, 8,  'Z', "pszEnd = s+8  (one PAST the Z at 7)");
        show(s, 7,  'Z', "pszEnd = s+7  (AT the Z at 7)");
        show(s, 4,  'Z', "pszEnd = s+4  (one PAST the Z at 3)");
        show(s, 3,  'Z', "pszEnd = s+3  (AT the Z at 3)");
        show(s, 0,  'Z', "pszEnd = s+0  (empty range)");
        printf("  => pszEnd is %s: \"AT the Z\" finding it means INCLUSIVE.\n",
               "read off above");
    }

    printf("\n=== the match value is a WORD; which bytes of it count? ===\n");
    {
        static const char s[] = "abcZdefZghi";
        show(s, -1, 'Z',    "wMatch = 0x005A ('Z')");
        show(s, -1, 0x015A, "wMatch = 0x015A (low byte 'Z')");
        show(s, -1, 0x5A5A, "wMatch = 0x5A5A (low byte 'Z')");
        show(s, -1, 0xFF5A, "wMatch = 0xFF5A (low byte 'Z')");
        show(s, -1, 0x5A00, "wMatch = 0x5A00 (low byte NUL)");
    }

    printf("\n=== searching for the TERMINATOR ===\n");
    {
        static const char s[] = "abc";
        show(s, -1, 0, "wMatch = 0, pszEnd = NULL");
        show(s, 3,  0, "wMatch = 0, pszEnd = s+3");
        /* Not probed, deliberately: StrRChrA(s, s+4, 0) -- searching for the terminator with an
           pszEnd placed one PAST it -- does not return. The first run of this probe sat in that
           call for 300 seconds and had to be killed. It is an out-of-contract input (pszEnd is
           exclusive and s+4 is past the end of the string), and a hang is not behaviour a caller
           can depend on, so this implementation does not reproduce it and correctness.c does not
           test it. Recorded here so nobody re-discovers it the hard way. */
        printf("  %-38s -> NOT PROBED: the live export does not return (see the comment above)\n",
               "wMatch = 0, pszEnd = s+4 (past the NUL)");
    }

    printf("\n=== degenerate cases ===\n");
    {
        static const char e[] = "";
        show(e, -1, 'Z', "empty string, absent");
        show(e, -1, 0,   "empty string, searching for NUL");
        static const char s[] = "abcZdefZghi";
        show(s, -1, 'Q', "target absent");
        /* Not probed, deliberately: pszEnd < pszStart. The range is empty by the exclusive rule
           established above, so there is nothing to learn, and a reverse scan starting below the
           string has nowhere to stop. Out of contract; not reproduced, not tested. */
        printf("  %-38s -> SKIPPED: out of contract (see the source)\n",
               "pszEnd BEFORE pszStart (s-1)");
        {
            char* r;
            printf("  %-38s -> ", "pszStart = NULL"); fflush(stdout);
            r = rchr(NULL, NULL, 'Z');
            printf("%s\n", r ? "non-NULL" : "NULL");
        }
    }

    printf("\n=== every byte value as the TARGET, including the high half ===\n");
    printf("(0x80..0xFF are ordinary characters in code page 1252 and must be findable.)\n");
    {
        int bad = 0;
        for (int b = 1; b < 256; ++b) {
            char t[8];
            /* The filler must not BE the target, or the string holds a third occurrence and the
               expected answer is not 3. The first run of this probe used 'a','b','c' as filler and
               duly reported byte 0x63 as an anomaly -- 0x63 is 'c', so "acbcc" really does have its
               last 'c' at offset 4 and the export was right. Filler that cannot collide: */
            char f = (char)((b == 0xF1) ? 0xF2 : 0xF1);
            t[0]=f; t[1]=(char)b; t[2]=f; t[3]=(char)b; t[4]=f; t[5]=0;
            char* r = rchr(t, NULL, (WORD)b);
            if (!r || (r - t) != 3) { if (bad < 8) printf("  target %02X -> %s%d (expected 3)\n",
                                        b, r ? "offset " : "NULL", r ? (int)(r-t) : -1); ++bad; }
        }
        printf("  %d of 255 byte values are not found at the LAST of two occurrences\n", bad);
    }

    printf("\n=== does it stop at the terminator when pszEnd runs past it? ===\n");
    printf("  ANSWER, AND IT EXPLAINS EVERYTHING ELSE: it does not return.\n");
    printf("  \"abc\\0ZZZZ\\0\" with pszEnd = t+9 sat there until the probe was killed, exactly as\n");
    printf("  \"abc\" with pszEnd = s+4 did. That pins the shipped algorithm: a FORWARD walk,\n");
    printf("      last = NULL;  p = pszStart;\n");
    printf("      while (p != end) { if (*p == (char)wMatch) last = p;  p = CharNextA(p); }\n");
    printf("  CharNextA does NOT advance past a terminator -- it returns the same pointer -- so when\n");
    printf("  `end` lies beyond the string's NUL the walk can never reach it and spins forever.\n");
    printf("  Every other measurement above falls out of that one loop:\n");
    printf("    * pszEnd is EXCLUSIVE, because the test is `p != end` before the body;\n");
    printf("    * searching for the TERMINATOR always returns NULL, because a valid range stops AT\n");
    printf("      or before the NUL and so never contains one;\n");
    printf("    * the cost is 16x the wide form's, because CharNextA is a call per character.\n");
    printf("  So the CONTRACT DOMAIN is: pszEnd == NULL, or pszStart <= pszEnd <= pszStart+strlen.\n");
    printf("  Outside it the export does not return, which is not behaviour a caller can depend on\n");
    printf("  and is not reproduced here.\n\n");
    if (0) {
        static char t[16];
        memcpy(t, "abc\0ZZZZ\0", 10);
        printf("  probing a pszEnd past an embedded NUL ...\n"); fflush(stdout);
        char* r = rchr(t, t + 9, 'Z');
        printf("  \"abc\\0ZZZZ\\0\", pszEnd = t+9 -> %s%d\n",
               r ? "offset " : "NULL ", r ? (int)(r - t) : -1);
        printf("  (offset 7 = it scanned PAST the embedded NUL; NULL = it stopped at it)\n");
        r = rchr(t, NULL, 'Z');
        printf("  same string, pszEnd = NULL   -> %s%d\n",
               r ? "offset " : "NULL ", r ? (int)(r - t) : -1);
    }
    return 0;
}
