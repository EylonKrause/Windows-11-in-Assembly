/* changes/236-pathcommonprefixa/probes/pcpa2.c
   THE DECISIVE PROBE: is PathCommonPrefixA's comparison ONE CHARACTER TO ONE CHARACTER?

   pcpa.c established that the comparison is not byte-exact. It folds 61 equivalence classes:
   A-Z with a-z, the whole CP1252 accented range 0xC0-0xDE with 0xE0-0xFE (0xD7 and 0xDF correctly
   excluded, being a multiplication sign and a letter with no uppercase), 0x8A/0x8C/0x8E/0x9F with
   their lowercase forms -- and ONE class that is not a case pair at all:

       0x5E ('^')  ==  0x88

   That is EXACTLY the defect discovery/shlwapi_narrow2.c recorded for StrStrA: "its comparison
   conflates 0x5E with 0x88, and a single 0x88 satisfies an UNBOUNDED RUN of needle 0x5E
   characters". The second half of that sentence is the dangerous half. A pairwise equivalence table
   -- which is all pcpa.c measured -- can be implemented in assembly. A comparison that matches ONE
   character against MANY, or against NONE, cannot: it is linguistic collation with expansions and
   ignorables, and no per-character fold reproduces it.

   pcpa.c could not tell those apart, because it only ever compared one character against one.

   This file settles it. Three tests, each built so that an expansion or a contraction changes the
   ANSWER and not merely the alignment:

     1. EXPANSION. a = "x\<v>\z" against b = "x\<w1><w2>\z". If the comparison is 1:1 these differ
        inside the second component and the common prefix is "x" -- one character. If <v> can stand
        for the pair <w1><w2>, the components match, the trailing "\z" lines up, and the prefix
        jumps to 5. All 256 x 256 x 256 combinations.

     2. IGNORABLES. a = "x\z\q" against b = "x\<v>z\q". If <v> is ignorable the two middle
        components are equal and the prefix reaches 4. All 256 values.

     3. THE StrStrA SHAPE DIRECTLY. a run of N copies of 0x5E as one component, against a single
        0x88 as that component, for N = 1..8. If one 0x88 satisfies the run, this is collation.

   If all three come back clean, the comparison is a per-character equivalence over 256 values and
   the whole function is a vectorisable fold-and-compare. If any one of them fires, 236 is PARKED
   with the evidence, exactly as StrStrA was. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *PCP)(LPCSTR, LPCSTR, LPSTR);
static PCP pcp;

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pcp = (PCP)GetProcAddress(hs, "PathCommonPrefixA");
    if (!pcp) { printf("cannot resolve PathCommonPrefixA\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    /* ---- 1. Expansion: can one character stand for two? ---------------------------------- */
    printf("=== 1. EXPANSION: does one character ever match two? ===\n");
    printf("  a = \"x\\<v>\\z\"   b = \"x\\<w1><w2>\\z\"\n");
    printf("  1:1 comparison -> prefix 1 (\"x\").  An expansion -> prefix 5.\n");
    printf("  All 256 x 256 x 256 combinations (NUL excluded).\n");
    {
        char a[16], b[16];
        long tested = 0, hits = 0;
        int shown = 0;
        a[0]='x'; a[1]='\\'; a[3]='\\'; a[4]='z'; a[5]=0;
        b[0]='x'; b[1]='\\'; b[4]='\\'; b[5]='z'; b[6]=0;
        for (int v = 1; v < 256; ++v) {
            if (v == '\\') continue;                 /* a separator is not a component character */
            a[2] = (char)v;
            for (int w1 = 1; w1 < 256; ++w1) {
                if (w1 == '\\') continue;
                b[2] = (char)w1;
                for (int w2 = 1; w2 < 256; ++w2) {
                    if (w2 == '\\') continue;
                    b[3] = (char)w2;
                    int r = pcp(a, b, NULL);
                    ++tested;
                    if (r > 1) {
                        ++hits;
                        if (shown < 10) {
                            printf("    EXPANSION 0x%02X == 0x%02X 0x%02X  -> prefix %d\n",
                                   v, w1, w2, r);
                            ++shown;
                        }
                    }
                }
            }
        }
        printf("    %ld combinations tested, %ld expansions\n", tested, hits);
        printf("    => %s\n", hits ? "COLLATION: one character can stand for two -- NOT vectorisable"
                                   : "no expansion: one character never matches two");
    }

    /* ---- 2. IGNORABLES: can a character match nothing? ------------------------------------ */
    printf("\n=== 2. IGNORABLES: does any character match nothing at all? ===\n");
    printf("  a = \"x\\z\\q\"   b = \"x\\<v>z\\q\"\n");
    printf("  1:1 comparison -> prefix 1.  An ignorable <v> -> prefix 4.\n");
    {
        char a[16], b[16];
        long hits = 0;
        strcpy(a, "x\\z\\q");
        b[0]='x'; b[1]='\\'; b[3]='z'; b[4]='\\'; b[5]='q'; b[6]=0;
        for (int v = 1; v < 256; ++v) {
            if (v == '\\') continue;
            b[2] = (char)v;
            int r = pcp(a, b, NULL);
            if (r > 1) { printf("    IGNORABLE 0x%02X -> prefix %d\n", v, r); ++hits; }
        }
        printf("    %ld ignorables over 255 byte values\n", hits);
        printf("    => %s\n", hits ? "COLLATION: a character can match nothing -- NOT vectorisable"
                                   : "no ignorables: every character must be matched");
    }

    /* ---- 3. The StrStrA shape, directly --------------------------------------------------- */
    printf("\n=== 3. the StrStrA shape: does ONE 0x88 satisfy a RUN of 0x5E? ===\n");
    {
        int bad = 0;
        for (int n = 1; n <= 8; ++n) {
            char a[32], b[32];
            int k = 0;
            a[k++]='x'; a[k++]='\\';
            for (int i = 0; i < n; ++i) a[k++] = 0x5E;
            a[k++]='\\'; a[k++]='z'; a[k]=0;
            b[0]='x'; b[1]='\\'; b[2]=(char)0x88; b[3]='\\'; b[4]='z'; b[5]=0;
            int r = pcp(a, b, NULL);
            /* n == 1 is the legitimate pairwise equivalence pcpa.c already found. n >= 2 firing
               would be the unbounded-run pathology. */
            printf("    %d x 0x5E vs one 0x88 -> prefix %d%s\n", n, r,
                   (n >= 2 && r > 1) ? "   <== UNBOUNDED RUN" : "");
            if (n >= 2 && r > 1) ++bad;
        }
        printf("    => %s\n", bad ? "COLLATION: the StrStrA pathology is present here too"
                                  : "the 0x5E/0x88 equivalence is STRICTLY PAIRWISE");
    }

    /* ---- 4. and the derived fold table, printed as the implementation will need it -------- */
    printf("\n=== 4. the fold, as a 256-entry table ===\n");
    printf("  (v -> the LOWEST byte value v compares equal to. This is what the assembly must\n");
    printf("   reproduce; it is DERIVED here, never assumed from any case-mapping API.)\n");
    {
        static unsigned char fold[256];
        for (int v = 0; v < 256; ++v) fold[v] = (unsigned char)v;
        for (int v = 1; v < 256; ++v) {
            if (v == '\\') continue;
            for (int w = 1; w < v; ++w) {
                if (w == '\\') continue;
                char a[8], b[8];
                a[0]='a'; a[1]=(char)v; a[2]='x'; a[3]=0;
                b[0]='a'; b[1]=(char)w; b[2]='x'; b[3]=0;
                if (pcp(a, b, NULL) >= 2) { fold[v] = fold[w]; break; }
            }
        }
        int moved = 0;
        for (int v = 1; v < 256; ++v) if (fold[v] != v) ++moved;
        printf("    %d of 255 byte values fold to a lower value\n", moved);
        /* Describe the fold as a rule rather than a table, and check that description exactly. */
        int plus20 = 0, other = 0;
        printf("    the exceptions to \"fold is -0x20\":\n");
        for (int v = 1; v < 256; ++v) {
            if (fold[v] == v) continue;
            if (fold[v] == v - 0x20) { ++plus20; continue; }
            printf("      0x%02X -> 0x%02X   (delta %+d)\n", v, fold[v], fold[v] - v);
            ++other;
        }
        printf("    %d values fold by exactly -0x20; %d do not\n", plus20, other);
        printf("\n    ranges that fold by -0x20:\n");
        {
            int start = -1;
            for (int v = 1; v <= 256; ++v) {
                int f = (v < 256) && (fold[v] == v - 0x20);
                if (f && start < 0) start = v;
                else if (!f && start >= 0) { printf("      0x%02X..0x%02X\n", start, v-1); start = -1; }
            }
        }
        printf("\n    values in 0x41..0xFE that do NOT fold and are not fold targets:\n      ");
        for (int v = 0x41; v < 256; ++v) {
            int is_target = 0;
            for (int w = 1; w < 256; ++w) if (w != v && fold[w] == v) { is_target = 1; break; }
            if (fold[v] == v && !is_target) printf("0x%02X ", v);
        }
        printf("\n");
    }
    return 0;
}
