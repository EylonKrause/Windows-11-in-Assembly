/* changes/263-rtlcompareunicodestrings/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll!RtlCompareUnicodeStrings.
 *
 * The exact value is compared, not the sign. probes/contract.c showed the export returns the
 * DIFFERENCE of the two characters -- -25 for `A` against `Z`, 65535 for U+FFFF against U+0000 --
 * and an implementation returning -1/0/1 would satisfy every caller that writes `< 0` and every
 * test that only checked the sign. So every case below compares the LONG itself.
 *
 *   1. EXHAUSTIVE over every ordered pair of single characters drawn from an alphabet that spans
 *      every interesting region of the table, both flags.
 *   2. EXHAUSTIVE over the ASCII quarter: all 128 x 128 ordered pairs, both flags. This is the
 *      range the in-vector fold claims to reproduce, so it is enumerated rather than sampled.
 *   3. Every length and every difference position around the block boundary -- 0..40 characters,
 *      with the difference planted at each position and past the end, which is where a vector tail
 *      or a block boundary goes wrong.
 *   4. UNEQUAL LENGTHS with a common prefix: the answer is len1 - len2 and not a sign, at lengths
 *      that straddle the sixteen-character block.
 *   5. The non-ascii fallback: strings whose differing block contains a character at or above
 *      0x80, so the block leaves the in-vector fold and goes through the table.
 *   6. A GUARD PAGE: both strings ending exactly at an inaccessible page, at every length.
 *   7. RANDOMISED over a mixed alphabet, both flags, unequal lengths.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG (NTAPI *F_Cmp)(const wchar_t*, SIZE_T, const wchar_t*, SIZE_T, BOOLEAN);

LONG wia_compareunicodestrings(const wchar_t*, SIZE_T, const wchar_t*, SIZE_T, BOOLEAN);
LONG ref_compareunicodestrings(const wchar_t*, SIZE_T, const wchar_t*, SIZE_T, BOOLEAN);
extern unsigned short wia_upcase[65536];
void wia_upcase_init(void);

static F_Cmp live;
static long fails = 0, cases = 0, n_eq = 0, n_ne = 0, n_bylen = 0;

static void one(const wchar_t* a, SIZE_T la, const wchar_t* b, SIZE_T lb, int ci, const char* where)
{
    LONG ro, rr, rl;
    ++cases;
    ro = wia_compareunicodestrings(a, la, b, lb, (BOOLEAN)ci);
    rr = ref_compareunicodestrings(a, la, b, lb, (BOOLEAN)ci);
    rl = live(a, la, b, lb, (BOOLEAN)ci);
    if (rl == 0) ++n_eq; else ++n_ne;
    if (ro != rl || rr != rl) {
        if (++fails <= 20)
            printf("  MISMATCH [%s] %s len %Iu vs %Iu  ours=%ld ref=%ld live=%ld  "
                   "(first chars U+%04X U+%04X)\n", where, ci ? "CI" : "cs", la, lb, ro, rr, rl,
                   la ? (unsigned)a[0] : 0, lb ? (unsigned)b[0] : 0);
    }
}

static unsigned long long rs = 0x243F6A8885A308D3ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    setvbuf(stdout, NULL, _IONBF, 0);
    live = (F_Cmp)GetProcAddress(h, "RtlCompareUnicodeStrings");
    if (!live) { printf("resolve failed\n"); return 1; }
    wia_upcase_init();

    printf("== CORRECTNESS: RtlCompareUnicodeStrings ==\n");
    printf("   the exact LONG is compared, not its sign\n");

    /* ---- 1. every ordered pair from an alphabet spanning the table ---- */
    {
        long before = cases;
        /* ASCII both cases, Latin-1 both cases, a character that folds by an offset other than 32,
           the dotless i (which does NOT fold to I), Greek, fullwidth, and the extremes */
        static const wchar_t A[] = {
            0x0000, 0x0001, 0x0040, 0x0041, 0x005A, 0x005B, 0x0060, 0x0061, 0x007A, 0x007B,
            0x007F, 0x0080, 0x00C0, 0x00DF, 0x00E0, 0x00FF, 0x0130, 0x0131, 0x0178, 0x01C5,
            0x03A3, 0x03C2, 0x03C3, 0x0410, 0x0430, 0x1E9E, 0x2170, 0x24B6, 0xFF21, 0xFF41,
            0xFFFE, 0xFFFF
        };
        int i, j, ci;
        for (i = 0; i < (int)(sizeof A / sizeof A[0]); ++i)
            for (j = 0; j < (int)(sizeof A / sizeof A[0]); ++j)
                for (ci = 0; ci < 2; ++ci) {
                    wchar_t x[1], y[1];
                    x[0] = A[i]; y[0] = A[j];
                    one(x, 1, y, 1, ci, "single characters across the table");
                }
        printf("  1. every ordered pair of 32 characters spanning the table, both flags: %ld\n",
               cases - before);
    }

    /* ---- 2. the ASCII quarter, exhaustively ---- */
    {
        long before = cases;
        unsigned i, j;
        for (i = 0; i < 128; ++i)
            for (j = 0; j < 128; ++j) {
                wchar_t x[1], y[1];
                x[0] = (wchar_t)i; y[0] = (wchar_t)j;
                one(x, 1, y, 1, 1, "ASCII pairs, case-insensitive");
            }
        printf("  2. ALL 128x128 ordered ASCII pairs, case-insensitive -- the range the in-vector\n"
               "     fold claims to reproduce, enumerated rather than sampled: %ld\n", cases - before);
    }

    /* ---- 3. every length and every difference position ---- */
    {
        long before = cases;
        static wchar_t a[64], b[64];
        int len, pos, ci, i;
        for (len = 0; len <= 40; ++len)
            for (pos = 0; pos <= len; ++pos)
                for (ci = 0; ci < 2; ++ci) {
                    for (i = 0; i < 64; ++i) { a[i] = L'q'; b[i] = L'q'; }
                    if (pos < len) b[pos] = L'r';
                    one(a, (SIZE_T)len, b, (SIZE_T)len, ci, "difference at every position");
                    if (pos < len) b[pos] = L'Q';   /* a CASE difference at the same place */
                    one(a, (SIZE_T)len, b, (SIZE_T)len, ci, "case difference at every position");
                }
        printf("  3. lengths 0..40 x a difference at every position x both flags -- the vector\n"
               "     tail and the block boundary live here: %ld\n", cases - before);
    }

    /* ---- 4. unequal lengths with a common prefix ---- */
    {
        long before = cases;
        static wchar_t a[80], b[80];
        int la, lb, i;
        for (i = 0; i < 80; ++i) { a[i] = L'z'; b[i] = L'z'; }
        for (la = 0; la <= 40; ++la)
            for (lb = 0; lb <= 40; ++lb) {
                one(a, (SIZE_T)la, b, (SIZE_T)lb, 0, "a common prefix, unequal lengths");
                if (la != lb) ++n_bylen;
            }
        printf("  4. every pair of lengths 0..40 over identical characters -- the answer is\n"
               "     len1-len2 in CHARACTERS, not a sign: %ld\n", cases - before);
    }

    /* ---- 5. the non-ASCII fallback ---- */
    {
        long before = cases;
        static wchar_t a[64], b[64];
        int pos, ci, i;
        static const wchar_t HIGH[6] = { 0x00E9, 0x0131, 0x03C3, 0x0430, 0xFF41, 0xFFFF };
        int k;
        for (k = 0; k < 6; ++k)
            for (pos = 0; pos < 40; ++pos)
                for (ci = 0; ci < 2; ++ci) {
                    for (i = 0; i < 64; ++i) { a[i] = L'm'; b[i] = L'm'; }
                    a[pos] = HIGH[k];                       /* forces the block off the fast fold */
                    one(a, 40, b, 40, ci, "a non-ASCII character in the differing block");
                    b[pos] = (wchar_t)wia_upcase[HIGH[k]];  /* ... and its upcase on the other side */
                    one(a, 40, b, 40, ci, "non-ASCII against its own upcase");
                }
        printf("  5. a character at or above 0x80 inside the block, at every position: %ld\n",
               cases - before);
    }

    /* ---- 6. a guard page ---- */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 4, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize * 3, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  6. guard page SKIPPED\n");
        } else {
            /* two strings, each ENDING exactly at the inaccessible page */
            int len, ci, i;
            for (len = 0; len <= 64; ++len) {
                wchar_t* x = (wchar_t*)(base + si.dwPageSize * 3) - len;
                wchar_t* y = (wchar_t*)(base + si.dwPageSize * 2) - len;
                for (i = 0; i < len; ++i) { x[i] = (wchar_t)(L'a' + (i & 15)); y[i] = x[i]; }
                for (ci = 0; ci < 2; ++ci) {
                    one(x, (SIZE_T)len, y, (SIZE_T)len, ci, "guard page, equal");
                    ++guard;
                    if (len) {
                        y[len - 1] = L'Z';
                        one(x, (SIZE_T)len, y, (SIZE_T)len, ci, "guard page, differing at the last");
                        y[len - 1] = x[len - 1];
                        ++guard;
                    }
                }
            }
            printf("  6. both strings ENDING at a PAGE_NOACCESS page, lengths 0..64: %ld cases, "
                   "no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* ---- 7. randomised ---- */
    {
        long before = cases;
        static wchar_t a[300], b[300];
        int trial, i;
        for (trial = 0; trial < 150000; ++trial) {
            int la = (int)(rnd() % 120), lb, mode = (int)(rnd() % 4);
            int ci = (int)(rnd() & 1);
            lb = (rnd() & 3) ? la : (int)(rnd() % 120);
            for (i = 0; i < 300; ++i) {
                unsigned r = rnd();
                a[i] = (mode == 0) ? (wchar_t)(L'a' + (r % 26))       /* lower ASCII only */
                     : (mode == 1) ? (wchar_t)(L'A' + (r % 26))       /* upper ASCII only */
                     : (mode == 2) ? (wchar_t)(r % 128)               /* all of ASCII */
                                   : (wchar_t)r;                      /* anything at all */
            }
            /* One trial in three is a copy that is equal all the way to the end, because that is
               the case that scans the WHOLE string -- the one the vector loop exists for, the one
               the headline bench row measures, and the one a corpus of random strings almost never
               produces. The first version of this corpus left the live export answering EQUAL on
               under 4% of its cases. */
            {
                int kind = trial % 3;
                for (i = 0; i < 300; ++i) {
                    unsigned r = rnd();
                    if (kind == 0) b[i] = a[i];                       /* exactly equal */
                    else if (kind == 1) {                             /* equal apart from case */
                        if (a[i] >= L'a' && a[i] <= L'z') b[i] = (wchar_t)(a[i] - 32);
                        else if (a[i] >= L'A' && a[i] <= L'Z') b[i] = (wchar_t)(a[i] + 32);
                        else b[i] = (wchar_t)wia_upcase[a[i]];
                    } else {                                          /* mostly a case-flip */
                        if ((r & 7) == 0) b[i] = (wchar_t)rnd();
                        else if (a[i] >= L'a' && a[i] <= L'z') b[i] = (wchar_t)(a[i] - 32);
                        else if (a[i] >= L'A' && a[i] <= L'Z') b[i] = (wchar_t)(a[i] + 32);
                        else b[i] = a[i];
                    }
                }
                if (kind != 2) lb = la;                               /* so the lengths agree too */
            }
            one(a, (SIZE_T)la, b, (SIZE_T)lb, ci, "randomised");
        }
        printf("  7. randomised, 4 alphabets, mostly case-flipped copies, unequal lengths: %ld\n",
               cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf("  the live export answered EQUAL %ld times and DIFFERENT %ld -- and %ld of the cases\n"
           "  were decided by the LENGTHS rather than by a character\n", n_eq, n_ne, n_bylen);
    if (!n_eq || !n_ne) { printf("CORRECTNESS: FAILED (a corpus never produced one of the answers)\n"); return 1; }
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (the exact LONG matches the oracle AND the live export)\n");
    return fails ? 1 : 0;
}
