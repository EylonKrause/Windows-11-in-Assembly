/* changes/288-foldstringw-digits/correctness.c
 *
 * Three-way: ours vs an independent scalar model vs the LIVE kernelbase export, for the MAP_FOLDDIGITS
 * path only, which is a measured scope, not a convenience. probes/contract.c folded every code unit
 * under every flag and found MAP_FOLDDIGITS to be the only strictly 1:1 one; the rest turn one unit into
 * as many as eighteen. Those flags are separate problems and this gate asserts that our code DECLINES
 * them rather than leaving the restriction to the corpus.
 *
 * What is compared: the return value, every output word, the word just past the end, and GetLastError on
 * every refusal. The last of those matters because the implementation writes the error straight to the
 * TEB at gs:[68h] instead of calling SetLastError, a stable offset, but one this gate verifies rather
 * than trusts.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (WINAPI *FFOLD)(DWORD, LPCWSTR, int, LPWSTR, int);

int wia_foldstringw_digits(DWORD, const wchar_t*, int, wchar_t*, int);
int ref_foldstringw_digits(DWORD, const wchar_t*, int, wchar_t*, int);
int wia_fold_init(void);
extern unsigned short wia_fold_digit[65536];
extern long wia_fold_changed;

#define MAPD 0x0080
#define SENT 0xA5A5

static FFOLD sys;
static long cases;
static int failures;

/* An ARBITRARY call (any flags, any pointer, any count) compared three ways on the return value
   and on GetLastError. one() below cannot express a NULL destination with a non-zero cchDest, nor a
   declined flag together with a bad pointer, and both of those turned out to matter: see section 7. */
static void three(const char* what, DWORD flags, const wchar_t* src, int cchSrc,
                  wchar_t* dest, int cchDest)
{
    int ra, rb, rc;
    DWORD ea, eb, ec;
    ++cases;
    SetLastError(0); ra = wia_foldstringw_digits(flags, src, cchSrc, dest, cchDest); ea = GetLastError();
    SetLastError(0); rb = sys(flags, src, cchSrc, dest, cchDest);                    eb = GetLastError();
    SetLastError(0); rc = ref_foldstringw_digits(flags, src, cchSrc, dest, cchDest); ec = GetLastError();
    if (ra != rb || ra != rc) {
        printf("  FAIL %s: return ours %d live %d model %d\n", what, ra, rb, rc);
        ++failures;
    } else if (ra == 0 && (ea != eb || ea != ec)) {
        printf("  FAIL %s: refused, but GetLastError ours %lu live %lu model %lu\n", what, ea, eb, ec);
        ++failures;
    }
}

/* One case, compared three ways including the error code and the word past the end. */
static void one(const wchar_t* s, int cchSrc, int cchDest, int expect_n)
{
    static wchar_t a[4200], b[4200], c[4200];
    int ra, rb, rc, i, n = expect_n;
    DWORD ea, eb, ec;
    ++cases;
    for (i = 0; i < 4200; ++i) a[i] = b[i] = c[i] = SENT;
    SetLastError(0); ra = wia_foldstringw_digits(MAPD, s, cchSrc, cchDest ? a : 0, cchDest); ea = GetLastError();
    SetLastError(0); rb = sys(MAPD, s, cchSrc, cchDest ? b : 0, cchDest);                    eb = GetLastError();
    SetLastError(0); rc = ref_foldstringw_digits(MAPD, s, cchSrc, cchDest ? c : 0, cchDest);  ec = GetLastError();
    if (ra != rb || ra != rc) {
        if (failures < 12)
            printf("  FAIL cchSrc=%d cchDest=%d: return ours %d live %d model %d\n",
                   cchSrc, cchDest, ra, rb, rc);
        ++failures;
        return;
    }
    if (!rb) {                                  /* all three refused: the error code must agree too */
        if (ea != eb || ea != ec) {
            if (failures < 12)
                printf("  FAIL cchSrc=%d cchDest=%d: refused, but GetLastError ours %lu live %lu "
                       "model %lu\n", cchSrc, cchDest, ea, eb, ec);
            ++failures;
        }
        return;
    }
    if (!cchDest) return;                       /* a length query writes nothing */
    if (n < 0) { if (cchSrc < 0) { n = 0; while (s[n]) ++n; ++n; } else n = cchSrc; }
    for (i = 0; i < n; ++i)
        if (a[i] != b[i] || a[i] != c[i]) {
            if (failures < 12)
                printf("  FAIL cchSrc=%d at %d (U+%04X): ours U+%04X live U+%04X model U+%04X\n",
                       cchSrc, i, (unsigned)s[i], (unsigned)a[i], (unsigned)b[i], (unsigned)c[i]);
            ++failures;
            return;
        }
    if (a[n] != SENT || b[n] != SENT || c[n] != SENT) {
        if (failures < 12)
            printf("  FAIL cchSrc=%d: a word past the end was written -- ours %04X live %04X "
                   "model %04X\n", cchSrc, (unsigned)a[n], (unsigned)b[n], (unsigned)c[n]);
        ++failures;
    }
}

int main(void)
{
    HMODULE kb = LoadLibraryW(L"kernelbase.dll");
    HMODULE k32 = LoadLibraryW(L"kernel32.dll");
    SYSTEM_INFO si;
    unsigned char* base;
    SIZE_T pg;
    int k, j;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (FFOLD)GetProcAddress(kb, "FoldStringW");
    if (!sys) sys = (FFOLD)GetProcAddress(k32, "FoldStringW");
    if (!sys) { printf("no FoldStringW\n"); return 2; }
    printf("== CORRECTNESS: kernelbase!FoldStringW, MAP_FOLDDIGITS ==\n");
    if (wia_fold_init()) {
        printf("  the table could not be derived from the live export, or a bulk call disagreed with\n"
               "  the per-character extraction\n");
        return 1;
    }
    printf("  0. the table derived from the live export and re-checked in bulk: %ld of 65535 code\n"
           "     units map to something else, the rest to themselves\n", wia_fold_changed);

    /* 1. every code unit, one at a time */
    {
        long before = cases;
        static wchar_t s[2];
        unsigned cu;
        for (cu = 1; cu < 65536; ++cu) { s[0] = (wchar_t)cu; s[1] = 0; one(s, 1, 8, 1); }
        printf("  1. every code unit 1..65535, one at a time: %ld\n", cases - before);
    }

    /* 2. every code unit again in bulk runs of 509 */
    {
        long before = cases;
        static wchar_t s[600];
        unsigned cu;
        for (cu = 1; cu < 65536; cu += 509) {
            int n = (cu + 509 <= 65536) ? 509 : (int)(65536 - cu);
            for (k = 0; k < n; ++k) s[k] = (wchar_t)(cu + k);
            s[n] = 0;
            one(s, n, 600, n);
            one(s, n, 0, n);                             /* the length query for the same input */
        }
        printf("  2. every code unit again in bulk runs of 509, and the same as a length query: %ld\n",
               cases - before);
    }

    /* 3. every alignment of the source and the destination */
    {
        long before = cases;
        static wchar_t pad[256];
        for (k = 0; k < 32; ++k) {
            wchar_t* s = pad + 32 + k;
            for (j = 0; j < 64; ++j) s[j] = (wchar_t)(0x0660 + (j % 10));   /* Arabic-Indic digits */
            s[64] = 0;
            one(s, 64, 600, 64);
            one(s, -1, 600, 65);
            one(s, 1, 600, 1);
            one(s, 17, 600, 17);
        }
        {
            static wchar_t src[80];
            static wchar_t dst[160];
            int off;
            for (j = 0; j < 40; ++j) src[j] = (wchar_t)(0x06F0 + (j % 10));
            src[40] = 0;
            for (off = 0; off < 8; ++off) {
                static wchar_t mine[160], live[160];
                int q, n;
                ++cases;
                for (q = 0; q < 160; ++q) mine[q] = live[q] = SENT;
                n = wia_foldstringw_digits(MAPD, src, 40, mine + off, 160 - off);
                if (n != sys(MAPD, src, 40, live + off, 160 - off)) {
                    printf("  FAIL dest alignment %d: lengths differ\n", off); ++failures;
                } else {
                    for (q = 0; q <= 40; ++q)
                        if (mine[off + q] != live[off + q]) {
                            printf("  FAIL dest alignment %d at %d\n", off, q); ++failures; break;
                        }
                }
            }
            (void)dst;
        }
        printf("  3. every source alignment x four counts, and every destination alignment: %ld\n",
               cases - before);
    }

    /* 4. cchSrc = -1 for every length, and the cchDest boundary in both directions */
    {
        long before = cases;
        static wchar_t s[80];
        int len;
        for (len = 1; len <= 40; ++len) {
            for (k = 0; k < len; ++k) s[k] = (wchar_t)(0x0660 + (k % 10));
            s[len] = 0;
            one(s, -1, 600, len + 1);
            one(s, -1, 0, len + 1);                      /* the query form */
            one(s, -1, len + 1, len + 1);                /* exactly enough */
            one(s, -1, len, 0);                          /* one too small: refused, nothing written */
            one(s, len, len, len);
            one(s, len, len - 1, 0);
            one(s, len, 0, len);
        }
        printf("  4. cchSrc = -1 for every length 1..40 (the terminator is folded too), with cchDest\n"
               "     exactly enough, one too small, and zero: %ld\n", cases - before);
    }

    /* 5. the refusals, including the error code and the exact-overlap rule */
    {
        long before = cases;
        static wchar_t s[] = L"abc123";
        static wchar_t d[64];
        static const DWORD badflags[] = { 0, 0x0010, 0x0020, 0x0040, 0x2000, 0x0090, 0x00A0,
                                          0x0060, 0x0081, 0xFFFFFFFFu };
        one(s, 0, 64, 0);                                /* cchSrc 0 -> ERROR_INSUFFICIENT_BUFFER */
        {
            int ra, rb, rc; DWORD ea, eb, ec;
            ++cases;
            SetLastError(0); ra = wia_foldstringw_digits(MAPD, 0, 3, d, 64); ea = GetLastError();
            SetLastError(0); rb = sys(MAPD, 0, 3, d, 64);                    eb = GetLastError();
            SetLastError(0); rc = ref_foldstringw_digits(MAPD, 0, 3, d, 64); ec = GetLastError();
            if (ra != rb || ra != rc || ea != eb || ea != ec) {
                printf("  FAIL NULL source: %d/%d/%d err %lu/%lu/%lu\n", ra, rb, rc, ea, eb, ec);
                ++failures;
            }
        }
        {
            /* dest == src is refused; every other overlap succeeds. Measured, and reproduced. */
            static wchar_t o[128];
            int ra, rb; DWORD ea, eb; int off;
            for (k = 0; k < 40; ++k) o[k] = (wchar_t)(0x0660 + (k % 10));
            o[40] = 0;
            ++cases;
            SetLastError(0); ra = wia_foldstringw_digits(MAPD, o, 8, o, 64); ea = GetLastError();
            SetLastError(0); rb = sys(MAPD, o, 8, o, 64);                    eb = GetLastError();
            if (ra != rb || ra != 0 || ea != eb) {
                printf("  FAIL dest == src: %d/%d err %lu/%lu\n", ra, rb, ea, eb); ++failures;
            }
            /* every overlap, in both directions, at four lengths.
             *
             * The first draft of this loop tested one length (8) and one direction (dest above src) at
             * offsets 1..8, and two mutants survived it by exploiting exactly what it did not reach.
             *
             * Mutant #28 measured the overlap span in code units instead of bytes, so it stops detecting
             * overlap once the offset reaches half the length, untested above, because with a length of
             * 8 the undetected offsets are 4..7 and at those offsets the unrolled loop happens to agree
             * with the naive one. Mutant #30 masked the unroll boundary with 15 instead of 7, moving the
             * split between the unrolled body and the tail. Both turned out to be provably equivalent
             * (see RESULTS.md), but that was established by argument AFTER the fact, and the argument
             * needs the measurements this loop now takes: several lengths, so the half-length threshold
             * lands at different offsets; offsets past the sub-group size of four; lengths that are not
             * multiples of eight or sixteen, so the tail is exercised at both splits; and a destination
             * BELOW the source, which the draft never tried at all even though probes/overlap.c had
             * already shown src-4 to be accepted by the export.
             *
             * The comparison is against the LIVE EXPORT, not against the model, the model's forward
             * loop was derived from the export for exactly this case, so comparing to it would be
             * comparing an assumption to itself. */
            {
                static const int lens[] = { 8, 16, 17, 40 };
                int li;
                for (li = 0; li < (int)(sizeof(lens) / sizeof(lens[0])); ++li) {
                    int n = lens[li];
                    for (off = 1; off <= 24; ++off) {
                        static wchar_t m1[256], m2[256];
                        int q, n1, n2;
                        ++cases;
                        for (q = 0; q < 256; ++q) m1[q] = m2[q] = (wchar_t)(0x0660 + (q % 10));
                        n1 = wia_foldstringw_digits(MAPD, m1 + 64, n, m1 + 64 + off, 128);
                        n2 = sys(MAPD, m2 + 64, n, m2 + 64 + off, 128);
                        if (n1 != n2) {
                            printf("  FAIL overlap len %d +%d: %d vs %d\n", n, off, n1, n2); ++failures;
                        } else for (q = 0; q < 256; ++q)
                            if (m1[q] != m2[q]) {
                                printf("  FAIL overlap len %d +%d at %d: ours %04X live %04X\n",
                                       n, off, q, m1[q], m2[q]);
                                ++failures; break;
                            }
                        /* and the same offset the other way: the destination BELOW the source */
                        ++cases;
                        for (q = 0; q < 256; ++q) m1[q] = m2[q] = (wchar_t)(0x0660 + (q % 10));
                        n1 = wia_foldstringw_digits(MAPD, m1 + 64, n, m1 + 64 - off, 128);
                        n2 = sys(MAPD, m2 + 64, n, m2 + 64 - off, 128);
                        if (n1 != n2) {
                            printf("  FAIL overlap len %d -%d: %d vs %d\n", n, off, n1, n2); ++failures;
                        } else for (q = 0; q < 256; ++q)
                            if (m1[q] != m2[q]) {
                                printf("  FAIL overlap len %d -%d at %d: ours %04X live %04X\n",
                                       n, off, q, m1[q], m2[q]);
                                ++failures; break;
                            }
                    }
                }
            }
        }
        /* The scope, asserted: every other flag combination must be declined by our code, and the live
           export accepts most of them -- so this is the one place the two deliberately differ, and it
           is checked rather than assumed. */
        for (k = 0; k < (int)(sizeof(badflags) / sizeof(badflags[0])); ++k) {
            int ra; DWORD ea;
            ++cases;
            SetLastError(0);
            ra = wia_foldstringw_digits(badflags[k], s, 6, d, 64);
            ea = GetLastError();
            if (ra != 0 || ea != ERROR_INVALID_FLAGS) {
                printf("  FAIL flags 0x%04lX: ours returned %d err %lu, expected 0 / 1004\n",
                       badflags[k], ra, ea);
                ++failures;
            }
        }
        printf("  5. cchSrc 0, a NULL source, dest == src, every overlap in BOTH directions at four\n"
               "     lengths and 24 offsets, and ten flag combinations this change declines by\n"
               "     design: %ld\n", cases - before);
    }

    /* 6. a guard page: the source's terminator is the last readable code unit */
    {
        long before = cases;
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  6. GUARD PAGE SETUP FAILED\n"); ++failures;
        } else {
            for (k = 1; k <= 60; ++k) {
                wchar_t* s = (wchar_t*)(base + pg) - k;          /* no terminator: an exact count */
                for (j = 0; j < k; ++j) s[j] = (wchar_t)(0x0660 + (j % 10));
                one(s, k, 600, k);
                one(s, k, 0, k);
            }
            for (k = 1; k <= 60; ++k) {
                wchar_t* s = (wchar_t*)(base + pg) - (k + 1);
                for (j = 0; j < k; ++j) s[j] = (wchar_t)(0x06F0 + (j % 10));
                s[k] = 0;                                        /* the terminator is last readable */
                one(s, -1, 600, k + 1);
            }
            printf("  6. a guard page: every length 1..60 with an exact count and no terminator, and\n"
                   "     with the terminator as the last readable code unit under -1: %ld\n",
                   cases - before);
        }
    }

    /* 7. The NULL destination and the whole refusal order.
     *
     * This section exists because of a mutation survivor. Mutant #10 deleted the NULL-destination
     * refusal from impl.asm and sections 1-6 above still passed 66,410 cases with 0 mismatches: not one
     * of them passes a NULL destination together with a non-zero cchDest, because one() derives the
     * destination from cchDest and can only ever pair NULL with 0. The corpus could not express the
     * case.
     *
     * Worse, the refusal had never been measured. It was written into impl.asm from the natural
     * assumption that a NULL destination must be refused, and reference.c took the same ordering from
     * impl.asm rather than from the export, so the three-way comparison was blind to it. An
     * assumption held by both sides of a comparison is invisible to that comparison; only asking the
     * export settles it, which is what probes/nulldest.c did.
     *
     * It found TWO things wrong. A NULL destination with a too-small cchDest gives 87, not 122, the
     * NULL test runs BEFORE the buffer test, and the draft had it after, a real defect. And an
     * unsupported flag reports 1004 alone but 87 when a pointer is also bad, the flag test runs
     * FIFTH, after the four pointer refusals, and the draft had it first.
     *
     * So the order below is asserted step by step, three ways, including the flags this change
     * declines, because with the flag test fifth this function now matches the export on every
     * refusal, declined flags included, and the declared divergence shrinks to exactly one input
     * class: a supported-but-unimplemented flag with wholly valid parameters. */
    {
        long before = cases;
        static wchar_t d[128];
        static const wchar_t* t3 = L"\x0660\x0661\x0662";
        static const DWORD declined[] = { 0x0000, 0x0010, 0x0020, 0x0040, 0x2000, 0x0001, 0x00A0 };
        int q;

        /* 7a. a NULL destination: legal with cchDest 0, refused with every non-zero cchDest, and the
               refusal must beat the buffer test rather than lose to it */
        three("dest NULL, cchSrc 3, cchDest 0", MAPD, t3, 3, 0, 0);
        three("dest NULL, cchSrc -1, cchDest 0", MAPD, t3, -1, 0, 0);
        three("dest NULL, cchSrc 3, cchDest 64", MAPD, t3, 3, 0, 64);
        three("dest NULL, cchSrc 3, cchDest 3 (exact)", MAPD, t3, 3, 0, 3);
        three("dest NULL, cchSrc 3, cchDest 2 (too small)", MAPD, t3, 3, 0, 2);
        three("dest NULL, cchSrc 3, cchDest 1", MAPD, t3, 3, 0, 1);
        three("dest NULL, cchSrc 1, cchDest 1", MAPD, t3, 1, 0, 1);
        three("dest NULL, cchSrc -1, cchDest 64", MAPD, t3, -1, 0, 64);
        three("dest NULL, cchSrc -1, cchDest 1 (too small)", MAPD, t3, -1, 0, 1);
        three("dest NULL, cchSrc 0, cchDest 64", MAPD, t3, 0, 0, 64);
        three("dest NULL, src NULL, cchDest 64", MAPD, 0, 3, 0, 64);
        three("dest NULL, src NULL, cchDest 0", MAPD, 0, 3, 0, 0);

        /* 7b. a real destination with cchDest 0 still writes nothing */
        for (k = 0; k < 8; ++k) d[k] = SENT;
        three("dest real, cchSrc 3, cchDest 0", MAPD, t3, 3, d, 0);
        ++cases;
        if (d[0] != SENT) { printf("  FAIL cchDest 0 wrote to the destination\n"); ++failures; }

        /* 7c. every declined flag against every bad pointer: the pointer error must win */
        for (q = 0; q < (int)(sizeof(declined) / sizeof(declined[0])); ++q) {
            three("declined flag, src NULL", declined[q], 0, 3, d, 64);
            three("declined flag, dest == src", declined[q], d, 3, d, 64);
            three("declined flag, cchSrc 0", declined[q], t3, 0, d, 64);
            three("declined flag, dest NULL, cchDest 64", declined[q], t3, 3, 0, 64);
            three("declined flag, dest NULL, cchDest 2", declined[q], t3, 3, 0, 2);
        }
        /* and with valid parameters the flag error must win over the buffer test */
        for (q = 0; q < (int)(sizeof(declined) / sizeof(declined[0])); ++q) {
            int ra; DWORD ea;
            ++cases;
            SetLastError(0);
            ra = wia_foldstringw_digits(declined[q], t3, 3, d, 2);   /* a too-small buffer as well */
            ea = GetLastError();
            if (ra != 0 || ea != ERROR_INVALID_FLAGS) {
                printf("  FAIL declined flag 0x%04lX with a too-small buffer: %d / %lu, expected 0 / 1004\n",
                       declined[q], ra, ea);
                ++failures;
            }
        }

        /* 7d. the refusals must not SCAN the string: an unterminated string ending exactly at a guard
               page, with cchSrc -1 and a destination the export refuses. The export returns 87 rather
               than faulting, so the refusal precedes the scan -- and if our order regresses, this
               case does not merely mismatch, it crashes. */
        if (base) {
            wchar_t* g = (wchar_t*)(base + pg) - 5;
            for (j = 0; j < 5; ++j) g[j] = (wchar_t)(0x0660 + j);   /* deliberately unterminated */
            three("guarded, cchSrc -1, dest NULL, cchDest 64", MAPD, g, -1, 0, 64);
            three("guarded, cchSrc -1, dest == src, cchDest 64", MAPD, g, -1, g, 64);
            three("guarded, cchSrc -1, cchSrc 0 form", MAPD, g, 0, d, 64);
            three("guarded, cchSrc -1, declined flag, dest NULL", 0x0010, g, -1, 0, 64);
            three("guarded, cchSrc 5, dest NULL, cchDest 4", MAPD, g, 5, 0, 4);
            three("guarded, cchSrc 5, dest real, cchDest 4", MAPD, g, 5, d, 4);
        } else {
            printf("  7. GUARD PAGE UNAVAILABLE for the no-scan cases\n"); ++failures;
        }

        printf("  7. the NULL destination at every cchDest, the measured refusal order step by step,\n"
               "     seven declined flags against four bad pointers each, and a guarded unterminated\n"
               "     string that a refusal must not scan: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    if (!failures)
        printf("CORRECTNESS: PASS (the return value, every output word, the word just past the end and\n"
               "GetLastError exact vs the live export and vs the scalar model, over every code unit one\n"
               "at a time and in bulk, every alignment, the cchDest boundary, the length-query form, the\n"
               "exact-overlap rule, the ten flag combinations this change declines, a guard page, and\n"
               "the measured refusal order including a NULL destination at every cchDest)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
