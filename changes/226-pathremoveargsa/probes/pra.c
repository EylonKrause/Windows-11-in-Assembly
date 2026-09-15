/* changes/226-pathremoveargsa/probes/pra.c
   Pin down shlwapi!PathRemoveArgsA before writing any assembly.

   Why: 69.96 ns against 24.63 ns for PathRemoveArgsW on the same character count
   (discovery/shlwapi_narrow2.c) -- 2.84x the wide cost for HALF the bytes.

   THE CONTRACT IS THE UNUSUAL PART. Change 175 derived it for the wide form over 2000000 fuzz
   cases, and it is not "cut at the first space". It has three behaviours, two of them surprising:

     1. find the first 0x20 OUTSIDE double quotes (each '"' toggles the state). Exactly 0x20
        splits; a TAB does not.
     2. if one exists AND something follows it: NUL it, and ALSO NUL the LAST byte of that run of
        spaces when a non-space follows. "ab   c" gets TWO terminators written, at 2 and at 4 --
        not at 2 and 3.
     3. if there is NO unquoted space: TRIM TRAILING BLANKS, terminating at the FIRST byte of the
        trailing run. This is why '"'+' ' is cut but '"'+' '+'a' is not.

   Behaviour 2 writes a byte PAST the terminator it just placed, which no string comparison can
   see. So every test here compares the WHOLE BUFFER against a poison fill.

   None of that is inherited. It is re-derived against the NARROW export, exhaustively, over the
   alphabet that makes all three behaviours reachable -- and with a TAB in it, because "exactly
   0x20 and not whitespace in general" is a claim that has to be measured, and because a corpus
   missing one character is exactly how eight landed changes in this repository shipped wrong.   */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void (WINAPI *FN)(char*);
static FN pra;

#define POISON '#'
#define NB 128

/* the rule change 175 derived for the wide form, transcribed to bytes */
static void model(char* psz){
    int n = 0; while (psz[n]) ++n;
    int q = 0, i = -1;
    for (int k = 0; k < n; ++k) {
        if (psz[k] == ' ' && !q) { i = k; break; }
        if (psz[k] == '"') q ^= 1;
    }
    if (i >= 0) {
        int args = i + 1;
        psz[i] = 0;
        if (psz[args] != 0) {
            int j = args;
            while (psz[j] == ' ') ++j;
            if (psz[j] != 0) psz[j-1] = 0;
        }
    } else {
        int j = n;
        while (j > 0 && psz[j-1] == ' ') --j;
        if (j < n) psz[j] = 0;
    }
}

static void dump(const char* b, int n){
    putchar('[');
    for (int i = 0; i < n; ++i) putchar(b[i] ? b[i] : '.');
    putchar(']');
}

static void show(const char* src){
    char a[NB], b[NB];
    int n = (int)strlen(src);
    memset(a, POISON, NB); memset(b, POISON, NB);
    memcpy(a, src, (size_t)n+1); memcpy(b, src, (size_t)n+1);
    pra(a); model(b);
    printf("  \"%s\"  live ", src); dump(a, n+2);
    printf("   model "); dump(b, n+2);
    printf("   %s\n", memcmp(a,b,NB)==0 ? "" : "  <<< DIFFER");
}

static int cmp1(const char* src){
    char a[NB], b[NB];
    int n = (int)strlen(src);
    memset(a, POISON, NB); memset(b, POISON, NB);
    memcpy(a, src, (size_t)n+1); memcpy(b, src, (size_t)n+1);
    pra(a); model(b);
    return memcmp(a, b, NB) == 0;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pra = (FN)GetProcAddress(hs, "PathRemoveArgsA");
    if (!pra) { printf("cannot resolve PathRemoveArgsA\n"); return 1; }
    printf("PathRemoveArgsA = %p\nGetACP() = %u\n", (void*)pra, GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. the three behaviours, spelled out ('.' shown for a NUL, '#' is poison) ===\n");
    show("");
    show("a");
    show("ab cd");
    show("ab   c");                 /* TWO terminators: at 2 and at 4 */
    show("ab   ");                  /* trailing run, nothing after it */
    show("ab ");
    show(" ab");
    show("  ab");
    show("\"a b\" c");              /* the quoted space does not split */
    show("\"a b\"");
    show("\"a b\" ");
    show("\"ab\" ");
    show("\" ");
    show("\" a");
    show("\"\" x");
    show("ab\tcd");                 /* a TAB must not split */
    show("ab\t");                   /* nor be trimmed */
    show("ab  \t  c");
    show("   ");
    show("\"\"\"");

    printf("\n=== 2. EXHAUSTIVE over {a, ' ', '\"', TAB}, lengths 0..9 ===\n");
    printf("  (whole-buffer compare -- behaviour 2 writes a byte PAST the terminator)\n");
    {
        static const char AL[4] = { 'a', ' ', '"', '\t' };
        char s[16];
        long total = 0, bad = 0;
        int shown = 0;
        for (int len = 0; len <= 9; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                if (!cmp1(s)) {
                    ++bad;
                    if (shown < 8) { printf("    MISMATCH "); show(s); ++shown; }
                }
                ++total;
            }
        }
        printf("    %ld strings: %ld mismatches\n", total, bad);
        printf("    => %s\n", bad ? "the narrow form does NOT carry the wide rule -- keep probing"
                                  : "the narrow export carries the wide rule exactly");
    }

    printf("\n=== 3. the STRONGER byte-wise screen ===\n");
    printf("  every byte value at each position the rule consults\n");
    {
        static const char* T[] = {
            "ab?cd ef",     /* before the split         */
            "ab ?cd",       /* right after the split    */
            "ab  ?cd",      /* inside the space run     */
            "?ab cd",       /* the first byte           */
            "ab cd?",       /* the last byte            */
            "\"a?b\" c",    /* inside the quoted region */
            "ab?",          /* the trailing position    */
            0
        };
        for (int t = 0; T[t]; ++t) {
            int bad = 0, shown = 0;
            for (int v = 1; v < 256; ++v) {
                char s[24];
                int n = (int)strlen(T[t]);
                for (int i = 0; i < n; ++i) s[i] = T[t][i] == '?' ? (char)v : T[t][i];
                s[n] = 0;
                if (!cmp1(s)) { ++bad; if (shown < 3) { printf("      0x%02X in \"%s\"\n", v, T[t]); ++shown; } }
            }
            printf("    %-12s %3d of 255 byte values disagree\n", T[t], bad);
        }
    }

    printf("\n=== 4. WHICH bytes split, and which are trimmed? ===\n");
    printf("  re-derived from scratch rather than inherited from the wide form\n");
    {
        int splitters = 0, trimmers = 0;
        for (int v = 1; v < 256; ++v) {
            char s[16];
            /* does it split "ab<v>cd"? */
            s[0]='a'; s[1]='b'; s[2]=(char)v; s[3]='c'; s[4]='d'; s[5]=0;
            {
                char a[NB]; memset(a, POISON, NB); memcpy(a, s, 6);
                pra(a);
                if ((int)strlen(a) != 5) { printf("    0x%02X SPLITS\n", v); ++splitters; }
            }
            /* is it trimmed from "ab<v>"? */
            s[0]='a'; s[1]='b'; s[2]=(char)v; s[3]=0;
            {
                char a[NB]; memset(a, POISON, NB); memcpy(a, s, 4);
                pra(a);
                if ((int)strlen(a) != 3) { printf("    0x%02X is TRIMMED from the end\n", v); ++trimmers; }
            }
        }
        printf("    %d byte values split, %d are trimmed\n", splitters, trimmers);
    }

    printf("\n=== 5. NULL ===\n");
    {
        printf("    ");
        pra(0);
        printf("PathRemoveArgsA(NULL) returned without faulting\n");
    }

    printf("\n=== 6. a MAX_PATH guard? lengths 250..270 with a space near the end ===\n");
    {
        static char big[400];
        int changed = 0, unchanged = 0;
        for (int len = 250; len <= 270; ++len) {
            char a[400], b[400];
            for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
            big[len-4] = ' ';
            big[len] = 0;
            memset(a, POISON, sizeof a); memcpy(a, big, (size_t)len+1);
            memset(b, POISON, sizeof b); memcpy(b, big, (size_t)len+1);
            pra(a);
            if (memcmp(a, b, sizeof a) == 0) { ++unchanged; printf("    length %d: UNCHANGED\n", len); }
            else ++changed;
        }
        printf("    %d cut, %d left alone => %s\n", changed, unchanged,
               unchanged ? "there IS a length guard -- find it exactly"
                         : "no MAX_PATH guard: it acts at every length");
    }

    printf("\n=== 7. long paths: 4000 bytes, space at many positions ===\n");
    {
        static char big[4200];
        int bad = 0;
        for (int pos = 1; pos < 4000; pos += 7) {
            for (int i = 0; i < 4000; ++i) big[i] = (char)('a' + i % 23);
            big[pos] = ' ';
            if (pos + 3 < 4000) { big[pos+1] = ' '; big[pos+2] = ' '; }
            big[4000] = 0;
            {
                char a[4300], b[4300];
                memset(a, POISON, sizeof a); memcpy(a, big, 4001);
                memset(b, POISON, sizeof b); memcpy(b, big, 4001);
                pra(a); model(b);
                if (memcmp(a, b, sizeof a) != 0) { if (!bad) printf("    MISMATCH at pos %d\n", pos); ++bad; }
            }
        }
        printf("    %d mismatches over spaces at 572 positions in a 4000-byte path\n", bad);
    }
    return 0;
}
