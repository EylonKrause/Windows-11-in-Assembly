/* changes/280-rtllargeintegertochar/correctness.c
 *
 * Gate 1 for ntdll!RtlLargeIntegerToChar: Ours vs the scalar model vs the live export, on the
 * NTSTATUS and every byte of a poison-filled buffer.
 *
 * The whole buffer is compared, on failing calls as well, because probes/contract.c measured that a
 * refusal leaves it untouched, and because the two success shapes differ in what they leave
 * BEHIND the answer:
 *
 *     a POSITIVE length writes the digits and a terminator only if one fits;
 *     a NEGATIVE length writes exactly -length characters, ZERO-PADDED on the left, and no
 *     terminator at all.
 *
 * That is exactly the distinction change 100 (the landed implementation of this export) gets
 * wrong on every single negative length, and it is invisible to any check that compares only the
 * status. Six of change 279's eleven mutants had identical statuses on both sides.
 *
 * The corpus is built where a 64-BIT length-first converter goes wrong:
 *
 *   * Every power of two, all sixty-four, one either side, the bit lengths where the digit count
 *     from BSR changes, in every base.
 *   * Every power of ten, all twenty, one either side, where the decimal correction fires.
 *   * THE 2^32 BOUNDARY, because the eight-digit peel is what separates the 64-bit reciprocal from
 *     067's 32-bit one, and a value just above it takes one more trip through the peel than a
 *     value just below.
 *   * Every MULTIPLE-OF-10^8 boundary reachable by the peel, because that divisor is the whole
 *     64-bit division and its remainder must be zero-padded to exactly eight characters. A chunk
 *     that dropped a leading zero would be right for most values and wrong for 10^8 itself.
 *   * every LENGTH from -80 to +80 at those boundaries. Base 2 runs to sixty-four characters, so a
 *     sweep to 40 would never reach its room rule at all.
 *   * every BASE 0..40, because only five are legal.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *F_LI2C)(LARGE_INTEGER*, ULONG, LONG, char*);

extern NTSTATUS wia_lint2char(const LARGE_INTEGER*, ULONG, LONG, char*);
extern void*    wia_li2c_tables(void);
extern void*    wia_li2c_offsets(void);
NTSTATUS ref_lint2char(const LARGE_INTEGER*, ULONG, LONG, char*);

static F_LI2C sys;
static int  failures = 0;
static long cases = 0;
static long n_ok = 0, n_inval = 0, n_over = 0, n_pad = 0, n_peel = 0;

#define BUF    320
#define POISON '#'

static void one(unsigned long long v, ULONG base, LONG len)
{
    static char ba[BUF], bb[BUF], bc[BUF];
    LARGE_INTEGER q;
    NTSTATUS ra, rb, rc;
    int bad = 0;

    q.QuadPart = (long long)v;
    memset(ba, POISON, BUF); memset(bb, POISON, BUF); memset(bc, POISON, BUF);
    ra = wia_lint2char(&q, base, len, ba);
    rb = sys(&q, base, len, bb);
    rc = ref_lint2char(&q, base, len, bc);

    ++cases;
    if (rb == 0) {
        ++n_ok;
        if (len < 0) ++n_pad;
        if (v > 0xFFFFFFFFull && (base == 10 || base == 0)) ++n_peel;
    }
    else if ((ULONG)rb == 0xC000000Dul) ++n_inval;
    else if ((ULONG)rb == 0x80000005ul) ++n_over;

    if (ra != rb || ra != rc) bad = 1;
    else if (memcmp(ba, bb, BUF) != 0) bad = 2;
    else if (memcmp(ba, bc, BUF) != 0) bad = 3;

    if (bad) {
        if (failures < 12) {
            int i;
            printf("  FAIL (%d) v=%llu base=%lu len=%ld: ours %08lX  live %08lX  model %08lX\n",
                   bad, v, (unsigned long)base, (long)len,
                   (unsigned long)ra, (unsigned long)rb, (unsigned long)rc);
            printf("       ours:");  for (i = 0; i < 26; ++i) printf(" %02X", (unsigned char)ba[i]);
            printf("\n       live:");  for (i = 0; i < 26; ++i) printf(" %02X", (unsigned char)bb[i]);
            printf("\n       modl:");  for (i = 0; i < 26; ++i) printf(" %02X", (unsigned char)bc[i]);
            printf("\n");
        }
        ++failures;
    }
}

static int check_tables(void)
{
    const unsigned char* tb = (const unsigned char*)wia_li2c_tables();
    const unsigned* off = (const unsigned*)wia_li2c_offsets();
    const unsigned short* dec2b = (const unsigned short*)(tb + off[0]);
    const char*           hexch = (const char*)          (tb + off[1]);
    const unsigned short* hex2  = (const unsigned short*)(tb + off[2]);
    const unsigned short* oct2  = (const unsigned short*)(tb + off[3]);
    const unsigned char*  bin8  =                         tb + off[4];
    const unsigned char*  gtab  =                         tb + off[5];
    const unsigned long long* thr = (const unsigned long long*)(tb + off[6]);
    const unsigned char*  c30   =                         tb + off[7];
    unsigned long long    m64   = *(const unsigned long long*)(tb + off[8]);
    int i, b, bad = 0;

    for (i = 0; i < 100; ++i) {
        unsigned short want = (unsigned short)(('0' + i / 10) | (('0' + i % 10) << 8));
        if (dec2b[i] != want) { printf("  the decimal pair table is wrong at %d\n", i); bad = 1; }
    }
    for (i = 0; i < 16; ++i) {
        char want = (char)(i < 10 ? '0' + i : 'A' + i - 10);
        if (hexch[i] != want) { printf("  the hex digit table is wrong at %d\n", i); bad = 1; }
    }
    for (i = 0; i < 256; ++i) {
        unsigned hi = (unsigned)i >> 4, lo = (unsigned)i & 15;
        unsigned short want = (unsigned short)((hi < 10 ? '0' + hi : 'A' + hi - 10) |
                              ((lo < 10 ? '0' + lo : 'A' + lo - 10) << 8));
        if (hex2[i] != want) { printf("  the hex-pair table is wrong at %d\n", i); bad = 1; }
    }
    for (i = 0; i < 64; ++i) {
        unsigned short want = (unsigned short)(('0' + (i >> 3)) | (('0' + (i & 7)) << 8));
        if (oct2[i] != want) { printf("  the octal-pair table is wrong at %d\n", i); bad = 1; }
    }
    for (i = 0; i < 256; ++i)
        for (b = 0; b < 8; ++b) {
            unsigned char want = (unsigned char)('0' + ((i >> (7 - b)) & 1));
            if (bin8[i * 8 + b] != want) {
                printf("  the binary byte table is wrong at %d, bit %d\n", i, b); bad = 1;
            }
        }
    /* GTAB spans SIXTY-FOUR bit lengths here, not thirty-two. An implementation that reused change
       279's table would read past its end on any value above 2^32 and produce a digit count from
       whatever followed it in .const. */
    for (i = 0; i < 64; ++i) {
        unsigned long long v = 1ull << i, t = 10;
        int n = 1;
        while (v >= t) { ++n; if (n >= 20) break; t *= 10; }
        if (gtab[i] != n) { printf("  the digit-count table is wrong at %d: %u vs %d\n",
                                   i, gtab[i], n); bad = 1; }
    }
    /* The threshold table, recomputed the slow way for every one of the sixty-four bit lengths.
       It is indexed by the BIT LENGTH rather than by the digit count so that its load does not
       depend on GTAB's, and its last four entries are 10^19, which the assembler cannot compute
       in a signed 64-bit expression. Both of those are places a table can go quietly wrong. */
    for (i = 0; i < 64; ++i) {
        unsigned long long want = 1, k;
        for (k = 0; k < gtab[i]; ++k) want *= 10;      /* 10 ^ GTAB[i] */
        if (thr[i] != want) {
            printf("  THRESH[%d] = %llu, want 10^%u = %llu\n", i, thr[i], gtab[i], want);
            bad = 1;
        }
    }
    /* and the rule the two tables implement together, at every power of ten and either side */
    {
        unsigned long long p = 1;
        for (i = 0; i <= 19; ++i) {
            int e;
            for (e = -1; e <= 1; ++e) {
                unsigned long long v = p + e, t2 = 10;
                unsigned long idx;
                unsigned want = 1, got;
                if (i == 0 && e < 0) continue;
                while (v >= t2) { ++want; if (want >= 20) break; t2 *= 10; }
                _BitScanReverse64(&idx, v | 1);
                got = gtab[idx] + (v >= thr[idx] ? 1u : 0u);
                if (v < 10) got = 1;                    /* the one-digit fast path in impl.asm */
                if (got != want) {
                    printf("  the digit count for %llu is %u, want %u\n", v, got, want);
                    bad = 1;
                }
            }
            if (i < 19) p *= 10;
        }
    }
    for (i = 0; i < 16; ++i)
        if (c30[i] != '0') { printf("  the fill constant is wrong at %d\n", i); bad = 1; }
    /* the 64-bit reciprocal, re-derived: M must be exactly ceil(2^90 / 10^8) */
    {
        unsigned long long want = 12379400392853802749ull;
        if (m64 != want) { printf("  the 64-bit reciprocal is %llu, want %llu\n", m64, want);
                           bad = 1; }
        else {
            /* and it must actually divide: check it at the ends and at every power of ten */
            unsigned long long p = 1;
            int k;
            for (k = 0; k <= 19; ++k) {
                unsigned long long q = __umulh(p, m64) >> 26;
                if (q != p / 100000000ull) {
                    printf("  the reciprocal is wrong at 10^%d\n", k); bad = 1;
                }
                if (k < 19) p *= 10;
            }
            if ((__umulh(0xFFFFFFFFFFFFFFFFull, m64) >> 26) != 0xFFFFFFFFFFFFFFFFull / 100000000ull)
                { printf("  the reciprocal is wrong at 2^64-1\n"); bad = 1; }
        }
    }
    if (!bad)
        printf("  0. the eight assembler-generated tables and the 64-bit reciprocal match their\n"
               "     definitions (probes/div64.c proves the reciprocal over all 2^64)\n");
    return bad;
}

int main(void)
{
    static const ULONG BASES[] = { 0, 2, 8, 10, 16 };
    unsigned long long v;
    ULONG base;
    unsigned bi;
    LONG len;
    int k;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F_LI2C)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlLargeIntegerToChar");
    if (!sys) { printf("no RtlLargeIntegerToChar\n"); return 2; }
    printf("== CORRECTNESS: RtlLargeIntegerToChar ==\n");
    if (check_tables()) return 1;

    /* 1. every base 0..40, at several values and lengths including negative ones */
    {
        long before = cases;
        for (base = 0; base <= 40; ++base) {
            one(0, base, 80);
            one(1234567890123456789ull, base, 80);
            one(0xFFFFFFFFFFFFFFFFull, base, 80);
            one(1234567890123456789ull, base, 4);
            one(1234567890123456789ull, base, 0);
            one(1234567890123456789ull, base, -80);
            one(1234567890123456789ull, base, -4);
        }
        printf("  1. every base 0..40, seven lengths each: %ld\n", cases - before);
    }

    /* 2. Every power of two, all sixty-four, one either side, every length -80..+80 */
    {
        long before = cases;
        for (k = 0; k < 64; ++k) {
            unsigned long long mid = 1ull << k;
            for (bi = 0; bi < 5; ++bi)
                for (len = -80; len <= 80; ++len) {
                    one(mid, BASES[bi], len);
                    one(mid - 1, BASES[bi], len);
                }
        }
        printf("  2. every power of two, one either side, every length -80..80: %ld\n",
               cases - before);
    }

    /* 3. Every power of ten, all twenty, one either side, every length -80..+80 */
    {
        long before = cases;
        unsigned long long p = 1;
        for (k = 0; k <= 19; ++k) {
            for (bi = 0; bi < 5; ++bi)
                for (len = -80; len <= 80; ++len) {
                    one(p, BASES[bi], len);
                    if (p) one(p - 1, BASES[bi], len);
                }
            if (k < 19) p *= 10;
        }
        printf("  3. every power of ten, one either side, every length -80..80: %ld\n",
               cases - before);
    }

    /* 4. The 2^32 boundary and the 10^8 peel, which is what makes this change 64-bit at all */
    {
        long before = cases;
        static const unsigned long long SPECIAL[] = {
            0xFFFFFFFFull, 0x100000000ull, 0x100000001ull,
            99999999ull, 100000000ull, 100000001ull,
            9999999999999999ull, 10000000000000000ull, 10000000000000001ull,
            100000000ull * 100000000ull,                    /* 10^16 exactly: two empty chunks */
            100000000ull * 100000000ull + 1ull,
            184467440737ull * 100000000ull,                 /* the last full peel before 2^64 */
            0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull,
            0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFEull,
            12345678000000000ull, 1000000000000000000ull, 10000000000000000000ull
        };
        unsigned si;
        for (si = 0; si < sizeof SPECIAL / sizeof SPECIAL[0]; ++si)
            for (bi = 0; bi < 5; ++bi)
                for (len = -80; len <= 80; ++len)
                    one(SPECIAL[si], BASES[bi], len);
        printf("  4. the 2^32 boundary and every 10^8 peel boundary, every length: %ld\n",
               cases - before);
    }

    /* 5. the lengths that cannot be negated.
     *
     * INT_MIN is the only negative length that is a REFUSAL rather than a field width, and it is
     * here. Its neighbour is not, and that is deliberate: INT_MIN+1 is a field width of
     * 2147483647, and probes/contract.c measured that a width longer than the buffer runs off the
     * end, change 279's first corpus died of an access violation for exactly that reason. The
     * buffer here is 320 bytes and nothing asks for more than 200.
     */
    {
        long before = cases;
        for (bi = 0; bi < 5; ++bi) {
            one(1234567890123456789ull, BASES[bi], (LONG)0x80000000ul);
            one(0, BASES[bi], (LONG)0x80000000ul);
            one(0xFFFFFFFFFFFFFFFFull, BASES[bi], (LONG)0x80000000ul);
            one(0, BASES[bi], -1);
            one(0, BASES[bi], -2);
            one(0, BASES[bi], 1);
            one(0, BASES[bi], 2);
        }
        printf("  5. INT_MIN, the one negative length that refuses: %ld\n", cases - before);
    }

    /* 6. a dense sweep of the low values in every base, room and padded */
    {
        long before = cases;
        for (v = 0; v < 40000; ++v)
            for (bi = 0; bi < 5; ++bi) { one(v, BASES[bi], 80); one(v, BASES[bi], -80); }
        printf("  6. every value 0..39999, five bases, room and padded: %ld\n", cases - before);
    }

    /* 7. the whole 64-bit range, strided so that every one of the sixty-four bit lengths and every
     *    residue pattern through the 10^8 peel is visited many times over */
    {
        long before = cases;
        unsigned long long s = 0x9E3779B97F4A7C15ull;
        long i;
        for (i = 0; i < 120000; ++i) {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            v = s >> (i % 64);                       /* every bit length, repeatedly */
            for (bi = 0; bi < 5; ++bi) one(v, BASES[bi], 80);
            one(v, 10, -80);
            one(v, 16, -40);
            one(v, 2, -70);
        }
        printf("  7. 120000 pseudo-random values across every bit length: %ld\n", cases - before);
    }

    /* 8. every length 1..200 and -1..-200 on a one-digit and a twenty-digit value */
    {
        long before = cases;
        for (k = 1; k <= 200; ++k) {
            one(7, 10, k);                       one(7, 10, -k);
            one(0xFFFFFFFFFFFFFFFFull, 10, k);   one(0xFFFFFFFFFFFFFFFFull, 10, -k);
            one(0xFFFFFFFFFFFFFFFFull, 2, k);    one(0xFFFFFFFFFFFFFFFFull, 2, -k);
        }
        printf("  8. every length 1..200, padded and not, one digit and sixty-four: %ld\n",
               cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    printf("  the live export answered SUCCESS %ld (of which %ld were ZERO-PADDED by a negative\n"
           "  length, and %ld took a value above 2^32 through the eight-digit peel),\n"
           "  INVALID_PARAMETER %ld, BUFFER_OVERFLOW %ld\n", n_ok, n_pad, n_peel, n_inval, n_over);
    if (n_ok < 10000 || n_inval < 100 || n_over < 1000 || n_pad < 10000 || n_peel < 5000) {
        printf("  the corpus did not reach every outcome -- the gate fails without them\n");
        ++failures;
    }
    if (!failures)
        printf("CORRECTNESS: PASS (the status and every byte of the buffer exact vs live ntdll and\n"
               "vs the scalar model, room and zero-padded forms alike, on failing calls too)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
