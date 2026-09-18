/* changes/288-foldstringw-digits/probes/contract.c
 *
 * WHICH FoldStringW FLAGS ARE A TABLE, AND WHICH ARE NOT?
 *
 *     int FoldStringW(DWORD dwMapFlags, LPCWSTR lpSrcStr, int cchSrc, LPWSTR lpDestStr, int cchDest)
 *
 * discovery/uncovered_2026b.c measured MAP_FOLDDIGITS at 636.57 ns for 511 code units, 0.623 ns per byte.
 * But FoldStringW is five different functions behind one entry point, and they are not equally
 * tractable:
 *
 *     MAP_FOLDDIGITS       0x0080   digits of every script to ASCII 0-9    -- plausibly 1:1
 *     MAP_FOLDCZONE        0x0010   compatibility-zone characters          -- plausibly 1:1
 *     MAP_PRECOMPOSED      0x0020   combine base + accent into one unit    -- LENGTH CHANGING
 *     MAP_COMPOSITE        0x0040   split one unit into base + accent      -- LENGTH CHANGING
 *     MAP_EXPAND_LIGATURES 0x2000   one unit into several                  -- LENGTH CHANGING
 *
 * A mapping that changes the length is not a per-character table, and a mapping that depends on
 * neighbouring characters -- which composition inherently does -- is not one either. Change 287's
 * questions therefore have to be asked PER FLAG rather than once, and the answer decides the scope of
 * this change rather than whether it happens at all: the repository already carries four separate
 * changes for crypt32!CryptBinaryToStringA, one per output format, and materialize.py combines them.
 *
 * So this probe establishes, for each flag and for the combinations the export accepts:
 *
 *   1. which flags are accepted at all, alone and together;
 *   2. whether the output LENGTH ever differs from the input length;
 *   3. whether the mapping is CONTEXT-FREE -- a long string compared against the per-character result;
 *   4. whether it is LOCALE-INVARIANT across several thread locales;
 *   5. how many code units the 1:1 flags actually change, which is what decides whether a table is
 *      worth having at all;
 *   6. the cchSrc and cchDest conventions, including the query-for-length form.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (WINAPI *FFOLD)(DWORD, LPCWSTR, int, LPWSTR, int);
static FFOLD fold;

#define M_CZONE   0x0010
#define M_PRECOMP 0x0020
#define M_COMPOS  0x0040
#define M_DIGITS  0x0080
#define M_LIGAT   0x2000

static const struct { DWORD f; const char* n; } FLAGS[] = {
    { M_CZONE,   "MAP_FOLDCZONE" },
    { M_PRECOMP, "MAP_PRECOMPOSED" },
    { M_COMPOS,  "MAP_COMPOSITE" },
    { M_DIGITS,  "MAP_FOLDDIGITS" },
    { M_LIGAT,   "MAP_EXPAND_LIGATURES" },
    { M_CZONE | M_DIGITS, "CZONE|DIGITS" },
    { M_PRECOMP | M_DIGITS, "PRECOMPOSED|DIGITS" },
    { M_PRECOMP | M_COMPOS, "PRECOMPOSED|COMPOSITE" },
    { 0, "no flags at all" }
};

/* Clearing the last error before a call is the only way to tell a real one from a leftover. */
static void RESET(void) { SetLastError(0); }

static unsigned short map1[65536];   /* the per-character result for the flag under test, or 0xFFFF */
static unsigned char  len1[65536];   /* how many units one input unit produced */

/* Fold one code unit on its own; record the produced length and the first unit. */
static void one_unit(DWORD flag, unsigned c, int* outlen, unsigned* first)
{
    WCHAR in[2], out[64];
    int n;
    in[0] = (WCHAR)c; in[1] = 0;
    out[0] = 0xFFFF;
    n = fold(flag, in, 1, out, 64);
    *outlen = n;
    *first = (n > 0) ? (unsigned short)out[0] : 0xFFFFu;
}

int main(void)
{
    HMODULE kb = LoadLibraryW(L"kernelbase.dll");
    HMODULE k32 = LoadLibraryW(L"kernel32.dll");
    int fi;
    unsigned c;

    setvbuf(stdout, NULL, _IONBF, 0);
    fold = (FFOLD)GetProcAddress(kb, "FoldStringW");
    if (!fold) fold = (FFOLD)GetProcAddress(k32, "FoldStringW");
    if (!fold) { printf("no FoldStringW\n"); return 2; }

    printf("== FoldStringW: which flags are a per-character table? ==\n\n");

    printf("-- 1. which flag combinations are accepted, on a plain ASCII string\n");
    for (fi = 0; fi < (int)(sizeof(FLAGS) / sizeof(FLAGS[0])); ++fi) {
        WCHAR out[64];
        int n = fold(FLAGS[fi].f, L"abc123", 6, out, 64);
        printf("   %-24s (0x%04lX) -> %s", FLAGS[fi].n, FLAGS[fi].f,
               n > 0 ? "accepted" : "REFUSED");
        if (n > 0) printf(", %d units", n);
        else printf(", GetLastError %lu", GetLastError());
        printf("\n");
    }

    printf("\n-- 2. does the output LENGTH ever differ from the input length?  (per flag, all 65535\n");
    printf("      code units, one at a time)\n");
    for (fi = 0; fi < (int)(sizeof(FLAGS) / sizeof(FLAGS[0])); ++fi) {
        long grew = 0, shrank = 0, failed = 0, changed = 0;
        unsigned firstgrow = 0;
        int biggest = 1;
        if (fold(FLAGS[fi].f, L"a", 1, 0, 0) <= 0 && GetLastError() == ERROR_INVALID_FLAGS) {
            printf("   %-24s refused, not measured\n", FLAGS[fi].n);
            continue;
        }
        for (c = 1; c < 65536; ++c) {
            int n; unsigned f;
            one_unit(FLAGS[fi].f, c, &n, &f);
            if (n <= 0) { ++failed; continue; }
            if (n > 1) { ++grew; if (!firstgrow) firstgrow = c; if (n > biggest) biggest = n; }
            if (n < 1) ++shrank;
            if (n == 1 && f != c) ++changed;
        }
        printf("   %-24s grew %5ld (max %d units)  shrank %ld  failed %ld  changed 1:1 %ld\n",
               FLAGS[fi].n, grew, biggest, shrank, failed, changed);
        if (grew && firstgrow)
            printf("      first growth at U+%04X -- so this flag is NOT a per-character table\n",
                   firstgrow);
        else if (!grew && !shrank)
            printf("      strictly 1:1: a table is possible\n");
    }

    printf("\n-- 3. MAP_FOLDDIGITS in detail: build the table, then test CONTEXT-FREEDOM\n");
    {
        long changed = 0;
        for (c = 0; c < 65536; ++c) {
            int n; unsigned f;
            if (c == 0) { map1[0] = 0; len1[0] = 1; continue; }
            one_unit(M_DIGITS, c, &n, &f);
            len1[c] = (unsigned char)((n > 0 && n < 255) ? n : 0);
            map1[c] = (unsigned short)((n == 1) ? f : 0xFFFF);
            if (n == 1 && f != c) ++changed;
        }
        printf("   %ld of 65535 code units are mapped to something else; the rest map to themselves\n",
               changed);
        {
            static WCHAR s[4096], out[4200];
            unsigned long long rs = 0x9E3779B97F4A7C15ull;
            long trials, bad = 0, shown = 0;
            for (trials = 0; trials < 20000; ++trials) {
                int n, k, m;
                rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
                n = 1 + (int)((rs >> 32) % 2048);
                for (k = 0; k < n; ++k) {
                    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
                    s[k] = (WCHAR)(1 + (rs >> 32) % 65535);
                }
                s[n] = 0;
                m = fold(M_DIGITS, s, n, out, 4200);
                if (m != n) {
                    if (shown < 4) { printf("     LENGTH CHANGED: %d units in, %d out\n", n, m); ++shown; }
                    ++bad;
                    continue;
                }
                for (k = 0; k < n; ++k)
                    if ((unsigned short)out[k] != map1[(unsigned short)s[k]]) {
                        if (shown < 6) {
                            printf("     context: U+%04X at %d of %d gave U+%04X, alone U+%04X\n",
                                   (unsigned)s[k], k, n, (unsigned)out[k], map1[(unsigned short)s[k]]);
                            ++shown;
                        }
                        ++bad;
                    }
            }
            printf("   20000 random strings up to 2048 units: %ld context-dependent or length-changing\n",
                   bad);
            if (!bad) printf("   => MAP_FOLDDIGITS is a pure per-character table\n");
        }
    }

    printf("\n-- 4. LOCALE INVARIANCE of the MAP_FOLDDIGITS table\n");
    {
        static const LCID locales[] = { 0x0409, 0x0407, 0x041F, 0x0419, 0x0411, 0x0401, 0x0439 };
        LCID saved = GetThreadLocale();
        int i;
        for (i = 0; i < (int)(sizeof(locales) / sizeof(locales[0])); ++i) {
            long diff = 0;
            unsigned firstc = 0;
            if (!SetThreadLocale(locales[i])) { printf("   LCID 0x%04X: cannot set\n", locales[i]); continue; }
            for (c = 1; c < 65536; ++c) {
                int n; unsigned f;
                one_unit(M_DIGITS, c, &n, &f);
                if (n != len1[c] || (n == 1 && f != map1[c])) { if (!firstc) firstc = c; ++diff; }
            }
            printf("   LCID 0x%04X: %ld of 65535 entries differ%s\n",
                   locales[i], diff, diff ? "  <-- NOT INVARIANT" : "");
            if (diff) printf("      first at U+%04X\n", firstc);
        }
        SetThreadLocale(saved);
    }

    printf("\n-- 5. what MAP_FOLDDIGITS actually changes\n");
    {
        long shown = 0;
        for (c = 1; c < 65536 && shown < 24; ++c)
            if (map1[c] != 0xFFFF && map1[c] != c) {
                printf("   U+%04X -> U+%04X\n", c, map1[c]);
                ++shown;
            }
    }

    printf("\n-- 6. the cchSrc and cchDest conventions\n");
    printf("   (every call below RESETS the last error first. The first version of this section did\n");
    printf("    not, and reported ERROR_INSUFFICIENT_BUFFER for cchSrc = 0 -- which was the 122 left\n");
    printf("    behind by the preceding too-small-buffer call. The real answer is 87. A stale error is\n");
    printf("    indistinguishable from a real one unless it is cleared.)\n");
    {
        static WCHAR out[64];
        int n;
        printf("   cchSrc  3, cchDest 64 : %d\n", fold(M_DIGITS, L"abc", 3, out, 64));
        printf("   cchSrc -1, cchDest 64 : %d   (does -1 include the terminator?)\n",
               fold(M_DIGITS, L"abc", -1, out, 64));
        n = fold(M_DIGITS, L"abc", 3, 0, 0);
        printf("   cchDest 0 (query)     : %d   (the required length, with no write)\n", n);
        n = fold(M_DIGITS, L"abc", -1, 0, 0);
        printf("   cchSrc -1, cchDest 0  : %d\n", n);
        out[0] = 0xAAAA;
        n = RESET(), fold(M_DIGITS, L"abc", 3, out, 2);
        printf("   cchDest 2 (too small) : %d, GetLastError %lu, out[0] = %04X\n",
               n, GetLastError(), (unsigned)out[0]);
        printf("   cchSrc 0              : %d, GetLastError %lu\n",
               RESET(), fold(M_DIGITS, L"abc", 0, out, 64), GetLastError());
        printf("   NULL source           : %d, GetLastError %lu\n",
               RESET(), fold(M_DIGITS, 0, 3, out, 64), GetLastError());
        printf("   source == dest        : %d, GetLastError %lu   (overlap is documented as illegal)\n",
               RESET(), fold(M_DIGITS, out, 3, out, 64), GetLastError());
    }

    printf("\n-- 7. HOW MUCH overlap does it reject?  exact equality, or any intersection?\n");
    {
        /* The export refused source == dest. Whether it checks pointer EQUALITY or an actual range
           intersection decides what a replacement has to reproduce, and guessing would be the same
           mistake as assuming a prototype. */
        static WCHAR buf2[256];
        int k;
        for (k = 0; k < 64; ++k) buf2[k] = (WCHAR)(L'a' + (k % 26));
        buf2[64] = 0;
        printf("   dest == src                       : %d, err %lu\n",
               fold(M_DIGITS, buf2, 8, buf2, 64), GetLastError());
        printf("   dest == src + 1  (overlapping)    : %d, err %lu\n",
               fold(M_DIGITS, buf2, 8, buf2 + 1, 64), GetLastError());
        printf("   dest == src + 4  (overlapping)    : %d, err %lu\n",
               fold(M_DIGITS, buf2, 8, buf2 + 4, 64), GetLastError());
        printf("   dest == src + 8  (just touching)  : %d, err %lu\n",
               fold(M_DIGITS, buf2, 8, buf2 + 8, 64), GetLastError());
        printf("   dest == src + 64 (well clear)     : %d, err %lu\n",
               fold(M_DIGITS, buf2, 8, buf2 + 64, 64), GetLastError());
        printf("   dest == src - 4  (dest below src) : %d, err %lu\n",
               fold(M_DIGITS, buf2 + 8, 8, buf2 + 4, 64), GetLastError());
        printf("   (if only the first line is refused, the check is pointer EQUALITY, not an\n");
        printf("    intersection -- which is what a replacement then has to reproduce)\n");
    }

    return 0;
}
