/* changes/238-pathmakeprettya/probes/pmpa3.c
   The last unknowns in PathMakePrettyA: the length bound, and the two tables in full.

   WHERE pmpa2.c GOT TO. The function is not "lowercase the path" -- it is two different mappings
   applied to two different parts, gated by an ASCII-only predicate:

     * IT REFUSES if the string contains any byte in 'a'..'z'. EXACTLY those 26 values and nothing
       else -- not the CP1252 lowercase range, not digits, not punctuation. So a path containing
       0xE0 (a-grave) is NOT considered to contain a lowercase letter and is rewritten anyway.
     * INDEX 0 IS UPPERCASED, not skipped. pmpa2.c's first detector could not see this because the
       only way to observe it is with a byte that is lowercase in CP1252 but not in ASCII -- the
       exact set the refusal predicate ignores. 34 of 255 values move, and 34 is a closed form:
       0x9A/0x9C/0x9E (3) + 0xE0..0xF6 (23) + 0xF8..0xFE (7) + 0xFF (1).
     * INDEX 1 ONWARD IS LOWERCASED. 60 of 255 values move: 0x41..0x5A (26) + 0xC0..0xD6 (23) +
       0xD8..0xDE (7) + 0x8A/0x8C/0x8E (3) + 0x9F (1) = 60.
     * THE RETURN means "no ASCII lowercase letter was present", NOT "something changed": "123456"
       and "" and "\\\\" all return 1 while changing nothing.

   WHAT IS LEFT.

     1. THE LENGTH BOUND. pmpa2.c's length sweep -- which exists because change 236's MAX_PATH rule
        got past six probes that never enumerated length -- reported that at length 260 the string
        stops being fully rewritten while the return stays non-zero. That is the same shape as change
        236's bound and it needs the same treatment: the exact threshold, and what is written at it.
        A partial rewrite and a total refusal are different implementations.
     2. IS THE REFUSAL SCAN BOUNDED TOO? If the rewrite stops at 260, does a lowercase letter at
        index 300 still veto? Those are two separate scans and they need not share a bound.
     3. THE REFUSAL PREDICATE'S POSITION-INDEPENDENCE, including past the first 32-byte block.
     4. BOTH TABLES PRINTED IN FULL, so the assembly has something to be checked against. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *PMP)(char*);
static PMP pmp;

#define POISON 0xCD
static char buf[8192];
static char in[8192];

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pmp = (PMP)GetProcAddress(hs, "PathMakePrettyA");
    if (!pmp) { printf("cannot resolve PathMakePrettyA\n"); return 1; }
    printf("GetACP() = %u   MAX_PATH = %d\n\n", GetACP(), MAX_PATH);

    printf("=== 1. THE LENGTH BOUND, one character at a time ===\n");
    printf("  All-uppercase letters, no punctuation. Reported as: the return, how many indices from\n");
    printf("  1 on were actually lowercased, and the index of the FIRST one left alone.\n");
    {
        for (int n = 254; n <= 266; ++n) {
            for (int i = 0; i < n; ++i) in[i] = (char)('A' + i % 23);
            in[n] = 0;
            memcpy(buf, in, n + 1);
            int r = pmp(buf);
            int low = 0, firstleft = -1;
            for (int i = 1; i < n; ++i) {
                if (buf[i] == in[i] + 0x20) ++low;
                else if (firstleft < 0) firstleft = i;
            }
            printf("  len %3d: ret %d, %3d of %3d lowercased, first left alone %4d\n",
                   n, r, low, n - 1, firstleft);
        }
    }

    printf("\n=== 2. and well past it, to see whether it is a prefix or nothing at all ===\n");
    {
        for (int n = 300; n <= 700; n += 100) {
            for (int i = 0; i < n; ++i) in[i] = (char)('A' + i % 23);
            in[n] = 0;
            memcpy(buf, in, n + 1);
            int r = pmp(buf);
            int low = 0, firstleft = -1, lastlow = -1;
            for (int i = 1; i < n; ++i) {
                if (buf[i] == in[i] + 0x20) { ++low; lastlow = i; }
                else if (firstleft < 0) firstleft = i;
            }
            printf("  len %3d: ret %d, %3d lowercased, first left alone %4d, last lowercased %4d\n",
                   n, r, low, firstleft, lastlow);
        }
    }

    printf("\n=== 3. is the REFUSAL scan bounded the same way? ===\n");
    printf("  An all-uppercase path with ONE lowercase letter placed at increasing positions. If the\n");
    printf("  refusal scan is unbounded, every position vetoes; if it shares the rewrite's bound,\n");
    printf("  positions past it do not.\n");
    {
        int n = 600;
        for (int pos = 0; pos <= 560; pos += 40) {
            for (int i = 0; i < n; ++i) in[i] = (char)('A' + i % 23);
            in[pos] = 'q';
            in[n] = 0;
            memcpy(buf, in, n + 1);
            int r = pmp(buf);
            int changed = memcmp(buf, in, n) != 0;
            printf("  lowercase at %3d of %d: ret %d, changed %d%s\n", pos, n, r, changed,
                   r == 0 ? "   (vetoed)" : "   <== NOT vetoed");
        }
    }

    printf("\n=== 4. the refusal predicate at every position of a short path ===\n");
    printf("  (including past the first 32-byte block, where a vectorised scan could differ)\n");
    {
        int n = 70, bad = 0;
        for (int pos = 0; pos < n; ++pos) {
            for (int i = 0; i < n; ++i) in[i] = (i % 8 == 7) ? '\\' : (char)('A' + i % 23);
            in[pos] = 'q';
            in[n] = 0;
            memcpy(buf, in, n + 1);
            int r = pmp(buf);
            if (r != 0) { ++bad; if (bad <= 6) printf("    NOT vetoed at position %d\n", pos); }
        }
        printf("    a lowercase letter failed to veto at %d of %d positions\n", bad, n);
    }

    printf("\n=== 5. and EVERY byte value as the sole non-uppercase character ===\n");
    printf("  Which values veto, at a position well inside a 70-byte path rather than at index 2?\n");
    {
        int veto = 0;
        int vlist[300], nv = 0;
        for (int v = 1; v < 256; ++v) {
            int n = 70;
            for (int i = 0; i < n; ++i) in[i] = (char)('A' + i % 23);
            in[45] = (char)v;
            in[n] = 0;
            memcpy(buf, in, n + 1);
            int r = pmp(buf);
            if (r == 0) { ++veto; if (nv < 300) vlist[nv++] = v; }
        }
        printf("    %d of 255 values veto at index 45:", veto);
        for (int i = 0; i < nv; ++i) printf(" %02X", vlist[i]);
        printf("\n    (pmpa2.c found 26 at index 2 -- these must be the same 26)\n");
    }

    printf("\n=== 6. BOTH TABLES IN FULL ===\n");
    printf("  index 0 (UPPERCASED) and index 1 onward (LOWERCASED), for every byte value that moves.\n");
    {
        unsigned char up[256], lo[256];
        for (int v = 0; v < 256; ++v) { up[v] = (unsigned char)v; lo[v] = (unsigned char)v; }
        for (int v = 1; v < 256; ++v) {
            /* index 0: v first, then uppercase filler */
            char s[16];
            s[0]=(char)v; s[1]='B'; s[2]='C'; s[3]='D'; s[4]='E'; s[5]='F'; s[6]=0;
            memcpy(buf, s, 7);
            if (pmp(buf)) up[v] = (unsigned char)buf[0];
            /* index 1 */
            s[0]='A'; s[1]=(char)v; s[2]='C'; s[3]='D'; s[4]='E'; s[5]='F'; s[6]=0;
            memcpy(buf, s, 7);
            if (pmp(buf)) lo[v] = (unsigned char)buf[1];
        }
        int nup = 0, nlo = 0;
        printf("    UPPERCASE map (index 0):\n      ");
        for (int v = 1; v < 256; ++v) if (up[v] != v) { printf("%02X>%02X ", v, up[v]); ++nup; }
        printf("\n      %d values move\n", nup);
        printf("    LOWERCASE map (index >= 1):\n      ");
        for (int v = 1; v < 256; ++v) if (lo[v] != v) { printf("%02X>%02X ", v, lo[v]); ++nlo; }
        printf("\n      %d values move\n", nlo);
        /* and are they exact inverses on the values that move? */
        int noninv = 0;
        for (int v = 1; v < 256; ++v) {
            if (lo[v] != v && up[lo[v]] != v) ++noninv;
            if (up[v] != v && lo[up[v]] != v) ++noninv;
        }
        printf("    %d values where the two maps are NOT inverses of each other\n", noninv);
    }
    return 0;
}
