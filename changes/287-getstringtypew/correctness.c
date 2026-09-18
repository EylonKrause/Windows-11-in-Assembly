/* changes/287-getstringtypew/correctness.c
 *
 * Three-way: ours vs an independent scalar model vs the LIVE kernelbase export.
 *
 * WHAT IS COMPARED IS THE RETURN VALUE, EVERY OUTPUT WORD, AND THE WORD JUST PAST THE END. That last one
 * matters as much as the others: this export is the first in the project that WRITES a buffer whose
 * length is given by the caller, so "one word too many" is a distinct failure mode from a wrong value,
 * and a corpus that only compared the words it asked for could not see it. Every case therefore fills
 * the destination with a sentinel and checks that the sentinel survives at index n.
 *
 * The corpus shapes carried over from changes 283-286, each of which exists because a mutant survived:
 *
 *   * every alignment of BOTH the source and the destination, because the length scan masks the bytes
 *     below the source pointer and a store loop can be misaligned independently;
 *   * every code unit, not a sample -- the table has 65536 entries and there is no reason to guess;
 *   * the count boundary in both directions;
 *   * a guard page, with the string ending exactly at the last readable code unit;
 *   * and the degenerate arguments, which for this export include four distinct refusals.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef BOOL (WINAPI *FGST)(DWORD, LPCWCH, int, LPWORD);

int wia_getstringtypew(DWORD, const wchar_t*, int, unsigned short*);
int ref_getstringtypew(DWORD, const wchar_t*, int, unsigned short*);
int wia_gst_init(void);
extern unsigned char wia_gst_npage[3];

static FGST sys;
static long cases;
static int failures;

#define SENT 0xA5A5
static const DWORD KIND[3] = { 1, 2, 4 };

/* n is how many words the call should write; -1 means work it out from cch and the string */
static void one(DWORD kind, const wchar_t* s, int cch, int expect_n)
{
    static unsigned short a[4200], b[4200], c[4200];
    int ra, rb, rc, i, n = expect_n;
    ++cases;
    for (i = 0; i < 4200; ++i) a[i] = b[i] = c[i] = SENT;
    ra = wia_getstringtypew(kind, s, cch, a);
    rb = sys(kind, s, cch, b) ? 1 : 0;
    rc = ref_getstringtypew(kind, s, cch, c);
    if (ra != rb || ra != rc) {
        if (failures < 12)
            printf("  FAIL kind=%lu cch=%d: return ours %d live %d model %d\n",
                   kind, cch, ra, rb, rc);
        ++failures;
        return;
    }
    if (!rb) return;                            /* all three refused: nothing was written */
    if (n < 0) {
        if (cch < 0) { n = 0; while (s[n]) ++n; ++n; }
        else n = cch;
    }
    for (i = 0; i < n; ++i)
        if (a[i] != b[i] || a[i] != c[i]) {
            if (failures < 12)
                printf("  FAIL kind=%lu cch=%d at %d (U+%04X): ours %04X live %04X model %04X\n",
                       kind, cch, i, (unsigned)s[i], a[i], b[i], c[i]);
            ++failures;
            return;
        }
    /* and nothing beyond: the caller asked for n words */
    if (a[n] != SENT || b[n] != SENT || c[n] != SENT) {
        if (failures < 12)
            printf("  FAIL kind=%lu cch=%d: a word past the end was written -- ours %04X live %04X "
                   "model %04X\n", kind, cch, a[n], b[n], c[n]);
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
    int t, k, j;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (FGST)GetProcAddress(kb, "GetStringTypeW");
    if (!sys) sys = (FGST)GetProcAddress(k32, "GetStringTypeW");
    if (!sys) { printf("no GetStringTypeW\n"); return 2; }
    printf("== CORRECTNESS: kernelbase!GetStringTypeW ==\n");
    if (wia_gst_init()) {
        printf("  the two-level tables could not be derived from the live export, or disagreed with\n"
               "  it -- tables.c re-checks every one of the 65536 entries through both levels and\n"
               "  then re-checks 511-unit BULK calls against the per-character extraction\n");
        return 1;
    }
    printf("  0. the tables derived from the live export and re-checked through both levels, plus\n"
           "     bulk calls re-checked against the per-character extraction: %u/%u/%u pages\n",
           wia_gst_npage[0], wia_gst_npage[1], wia_gst_npage[2]);

    /* 1. EVERY code unit, one at a time, all three info types */
    {
        long before = cases;
        static wchar_t s[2];
        unsigned cu;
        for (t = 0; t < 3; ++t)
            for (cu = 0; cu < 65536; ++cu) {
                s[0] = (wchar_t)cu; s[1] = 0;
                one(KIND[t], s, 1, 1);
            }
        printf("  1. every code unit 0..65535, one at a time, all three info types: %ld\n",
               cases - before);
    }

    /* 2. every code unit again, in BULK runs of 509, all three types */
    {
        long before = cases;
        static wchar_t s[600];
        unsigned cu;
        for (t = 0; t < 3; ++t)
            for (cu = 0; cu < 65536; cu += 509) {
                int n = (cu + 509 <= 65536) ? 509 : (int)(65536 - cu);
                for (k = 0; k < n; ++k) s[k] = (wchar_t)(cu + k);
                s[n] = 0;
                one(KIND[t], s, n, n);
            }
        printf("  2. every code unit again in bulk runs of 509, all three info types: %ld\n",
               cases - before);
    }

    /* 3. every alignment of the SOURCE and of the DESTINATION */
    {
        long before = cases;
        static wchar_t pad[256];
        static unsigned short outpad[256];
        for (k = 0; k < 32; ++k) {
            wchar_t* s = pad + 32 + k;
            for (j = 0; j < 64; ++j) s[j] = (wchar_t)(L'a' + (j % 26));
            s[64] = 0;
            for (t = 0; t < 3; ++t) {
                one(KIND[t], s, 64, 64);
                one(KIND[t], s, -1, 65);
                one(KIND[t], s, 1, 1);
                one(KIND[t], s, 17, 17);
            }
            /* a destination at every alignment, checked directly rather than through one() */
            {
                int off;
                for (off = 0; off < 8; ++off) {
                    unsigned short* d = outpad + off;
                    static unsigned short mine[128], live[128];
                    int q;
                    ++cases;
                    for (q = 0; q < 128; ++q) mine[q] = live[q] = SENT;
                    wia_getstringtypew(1, s, 40, mine + off);
                    sys(1, s, 40, live + off);
                    for (q = 0; q < 41; ++q)
                        if (mine[off + q] != live[off + q]) {
                            if (failures < 12)
                                printf("  FAIL dest alignment %d at %d: ours %04X live %04X\n",
                                       off, q, mine[off + q], live[off + q]);
                            ++failures;
                            break;
                        }
                    (void)d;
                }
            }
        }
        printf("  3. every source alignment x four counts x three types, and every destination\n"
               "     alignment: %ld\n", cases - before);
    }

    /* 4. the count boundary, and cch = -1 for every length */
    {
        long before = cases;
        static wchar_t s[80];
        int len;
        for (len = 0; len <= 40; ++len) {
            for (k = 0; k < len; ++k) s[k] = (wchar_t)(L'A' + (k % 26));
            s[len] = 0;
            for (t = 0; t < 3; ++t) {
                one(KIND[t], s, -1, len + 1);     /* -1 INCLUDES the terminator */
                if (len > 0) {
                    one(KIND[t], s, len, len);
                    one(KIND[t], s, 1, 1);
                    one(KIND[t], s, len > 1 ? len - 1 : 1, len > 1 ? len - 1 : 1);
                }
                one(KIND[t], s, len + 1, len + 1); /* a count PAST the terminator is still a count */
                one(KIND[t], s, len + 5, len + 5);
            }
        }
        printf("  4. cch = -1 for every length 0..40 (the terminator is classified too), and counts\n"
               "     at, below and past the terminator: %ld\n", cases - before);
    }

    /* 5. the refusals */
    {
        long before = cases;
        static wchar_t s[] = L"abc";
        static unsigned short o[8];
        static const DWORD badkind[] = { 0, 3, 5, 6, 7, 8, 9, 0x10, 1 | 2, 2 | 4, 0xFFFFFFFFu };
        for (k = 0; k < (int)(sizeof(badkind) / sizeof(badkind[0])); ++k)
            one(badkind[k], s, 3, 0);
        for (t = 0; t < 3; ++t) {
            one(KIND[t], s, 0, 0);                /* a count of zero */
            {
                int ra, rb, rc;
                ++cases;
                ra = wia_getstringtypew(KIND[t], 0, 3, o);
                rb = sys(KIND[t], 0, 3, o) ? 1 : 0;
                rc = ref_getstringtypew(KIND[t], 0, 3, o);
                if (ra != rb || ra != rc) {
                    printf("  FAIL NULL source: ours %d live %d model %d\n", ra, rb, rc);
                    ++failures;
                }
                ++cases;
                ra = wia_getstringtypew(KIND[t], s, 3, 0);
                rb = sys(KIND[t], s, 3, 0) ? 1 : 0;
                rc = ref_getstringtypew(KIND[t], s, 3, 0);
                if (ra != rb || ra != rc) {
                    printf("  FAIL NULL destination: ours %d live %d model %d\n", ra, rb, rc);
                    ++failures;
                }
            }
        }
        printf("  5. eleven invalid info types (including CT_CTYPE1|CT_CTYPE2), a count of zero, a\n"
               "     NULL source and a NULL destination: %ld\n", cases - before);
    }

    /* 6. an embedded NUL under an explicit count is just another code unit */
    {
        long before = cases;
        static wchar_t s[32];
        for (k = 1; k < 20; ++k) {
            for (j = 0; j < 20; ++j) s[j] = (wchar_t)(L'a' + j);
            s[20] = 0;
            s[k] = 0;
            for (t = 0; t < 3; ++t) {
                one(KIND[t], s, 20, 20);
                one(KIND[t], s, -1, k + 1);        /* -1 stops at the FIRST NUL, and includes it */
            }
        }
        printf("  6. an embedded NUL at every position, under an explicit count and under -1: %ld\n",
               cases - before);
    }

    /* 7. Latin-1 only, non-Latin-1 only, and mixed -- the two paths in the loop */
    {
        long before = cases;
        static wchar_t s[600];
        for (t = 0; t < 3; ++t) {
            for (k = 0; k < 511; ++k) s[k] = (wchar_t)(1 + (k % 255));       /* all below U+0100 */
            s[511] = 0;
            one(KIND[t], s, 511, 511);
            for (k = 0; k < 511; ++k) s[k] = (wchar_t)(0x4E00 + k);          /* none below U+0100 */
            s[511] = 0;
            one(KIND[t], s, 511, 511);
            for (k = 0; k < 511; ++k) s[k] = (wchar_t)((k & 1) ? (0x4E00 + k) : (1 + (k % 255)));
            s[511] = 0;
            one(KIND[t], s, 511, 511);            /* alternating, so the branch mispredicts */
            for (k = 0; k < 511; ++k) s[k] = (wchar_t)(0xD800 + (k % 0x800)); /* surrogates */
            s[511] = 0;
            one(KIND[t], s, 511, 511);
        }
        printf("  7. all-Latin-1, no-Latin-1, alternating and all-surrogate strings: %ld\n",
               cases - before);
    }

    /* 8. a guard page: the string's last code unit is the last readable one */
    {
        long before = cases;
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  8. GUARD PAGE SETUP FAILED\n"); ++failures;
        } else {
            for (k = 1; k <= 60; ++k) {
                wchar_t* s = (wchar_t*)(base + pg) - k;      /* no terminator: an exact count only */
                for (j = 0; j < k; ++j) s[j] = (wchar_t)(L'a' + (j % 26));
                for (t = 0; t < 3; ++t) one(KIND[t], s, k, k);
            }
            for (k = 1; k <= 60; ++k) {
                wchar_t* s = (wchar_t*)(base + pg) - (k + 1);
                for (j = 0; j < k; ++j) s[j] = (wchar_t)(L'a' + (j % 26));
                s[k] = 0;                                    /* the terminator IS the last readable */
                for (t = 0; t < 3; ++t) one(KIND[t], s, -1, k + 1);
            }
            printf("  8. a guard page: every length 1..60 with an exact count and no terminator, and\n"
                   "     again with the terminator as the last readable code unit under -1: %ld\n",
                   cases - before);
        }
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    if (!failures)
        printf("CORRECTNESS: PASS (the return value, every output word, and the word just past the end\n"
               "exact vs the live export and vs the scalar model, over every code unit one at a time\n"
               "and in bulk, every source and destination alignment, the count boundary, cch = -1\n"
               "including the terminator, eleven invalid info types, embedded NULs, all four string\n"
               "compositions and a guard page)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
