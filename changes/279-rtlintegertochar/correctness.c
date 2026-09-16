/* changes/279-rtlintegertochar/correctness.c
 *
 * Gate 1 for ntdll!RtlIntegerToChar: OURS vs THE SCALAR MODEL vs THE LIVE EXPORT, on the NTSTATUS
 * and every byte of a poison-filled buffer.
 *
 * THE WHOLE BUFFER IS COMPARED, on failing calls as well, because probes/contract.c measured that a
 * refusal leaves it untouched -- and because the two success shapes differ in what they leave
 * BEHIND the answer:
 *
 *     a POSITIVE length writes the digits and a terminator only if one fits;
 *     a NEGATIVE length writes exactly -length characters, ZERO-PADDED on the left, and no
 *     terminator at all.
 *
 * An implementation that terminated the padded form, or padded the room form, would produce the
 * right digits and the wrong buffer. Only a byte-for-byte comparison sees it.
 *
 * THE CORPUS IS BUILT WHERE A LENGTH-FIRST CONVERTER AND A FIELD WIDTH GO WRONG:
 *
 *   * EVERY LENGTH from -40 to +40 at every digit-count boundary of every base. The room rule and
 *     the padding rule are each decided one byte at a time, and the two meet at zero.
 *   * EVERY BASE 0..40, because only five are legal.
 *   * EVERY POWER OF EVERY BASE, one either side, because that is where the digit count changes.
 *   * INT_MIN and INT_MIN+1, because one can be negated and the other cannot.
 *   * and the whole 32-bit value range on a prime stride.
 *
 * The buffer is 256 bytes and the negative lengths stay well inside it: probes/negative.c showed
 * that -100 really does write a hundred characters, and faults if the caller's buffer is shorter.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *F_I2C)(ULONG, ULONG, LONG, char*);

extern NTSTATUS wia_int2char(ULONG, ULONG, LONG, char*);
extern void*    wia_i2c_tables(void);
extern void*    wia_i2c_tables2(void);
NTSTATUS ref_int2char(ULONG, ULONG, LONG, char*);

static F_I2C sys;
static int  failures = 0;
static long cases = 0;
static long n_ok = 0, n_inval = 0, n_over = 0, n_pad = 0;

#define BUF    256
#define POISON '#'

static void one(ULONG v, ULONG base, LONG len)
{
    static char ba[BUF], bb[BUF], bc[BUF];
    NTSTATUS ra, rb, rc;
    int bad = 0;

    memset(ba, POISON, BUF); memset(bb, POISON, BUF); memset(bc, POISON, BUF);
    ra = wia_int2char(v, base, len, ba);
    rb = sys(v, base, len, bb);
    rc = ref_int2char(v, base, len, bc);

    ++cases;
    if (rb == 0) { ++n_ok; if (len < 0) ++n_pad; }
    else if ((ULONG)rb == 0xC000000Dul) ++n_inval;
    else if ((ULONG)rb == 0x80000005ul) ++n_over;

    if (ra != rb || ra != rc) bad = 1;
    else if (memcmp(ba, bb, BUF) != 0) bad = 2;
    else if (memcmp(ba, bc, BUF) != 0) bad = 3;

    if (bad) {
        if (failures < 12) {
            int i;
            printf("  FAIL (%d) v=%lu base=%lu len=%ld: ours %08lX  live %08lX  model %08lX\n",
                   bad, (unsigned long)v, (unsigned long)base, (long)len,
                   (unsigned long)ra, (unsigned long)rb, (unsigned long)rc);
            printf("       ours:");  for (i = 0; i < 24; ++i) printf(" %02X", (unsigned char)ba[i]);
            printf("\n       live:");  for (i = 0; i < 24; ++i) printf(" %02X", (unsigned char)bb[i]);
            printf("\n       modl:");  for (i = 0; i < 24; ++i) printf(" %02X", (unsigned char)bc[i]);
            printf("\n");
        }
        ++failures;
    }
}

static int check_tables(void)
{
    const unsigned short* d2b = (const unsigned short*)wia_i2c_tables();
    const unsigned char* tabs = (const unsigned char*)d2b + 208;   /* 100 words, 16-byte aligned */
    const unsigned long long* pow10 = (const unsigned long long*)tabs;
    const unsigned char* g = tabs + 88;
    const char* hex = (const char*)(tabs + 120);
    int i, s, b, bad = 0;

    for (i = 0; i < 100; ++i) {
        unsigned short want = (unsigned short)(('0' + i / 10) | (('0' + i % 10) << 8));
        if (d2b[i] != want) { printf("  the digit-pair table is wrong at %d\n", i); bad = 1; }
    }
    {
        unsigned long long p = 1;
        for (i = 0; i <= 10; ++i) {
            if (pow10[i] != p) { printf("  the power-of-ten table is wrong at %d\n", i); bad = 1; }
            p *= 10;
        }
    }
    for (i = 0; i < 32; ++i) {
        unsigned long long v = 1ull << i, t = 10;
        int n = 1;
        while (v >= t) { t *= 10; ++n; }
        if (g[i] != n) { printf("  the digit-count table is wrong at %d\n", i); bad = 1; }
    }
    for (i = 0; i < 16; ++i) {
        char want = (char)(i < 10 ? '0' + i : 'A' + i - 10);
        if (hex[i] != want) { printf("  the hex table is wrong at %d\n", i); bad = 1; }
    }
    /* THE POWER-OF-TWO DIGIT COUNT IS NO LONGER A TABLE -- it is BSR, one shift or one multiply,
       and a lea. So it is checked the only way an arithmetic identity can be: over EVERY value
       whose digit count could differ, which for a power-of-two base is every power of two and its
       two neighbours, in all three bases. A wrong constant in the divide-by-3 shows up at the
       third octal boundary and a wrong shift at the first hexadecimal one. */
    for (s = 0; s < 32; ++s) {
        static const unsigned SH[3] = { 1, 3, 4 };
        unsigned long long mid = 1ull << s;
        unsigned d;
        for (d = 0; d < 3; ++d) {
            unsigned e;
            for (e = 0; e < 3; ++e) {
                unsigned long long v = mid + e - 1;
                unsigned idx = 0, want, got;
                unsigned long long w;
                if (v > 0xFFFFFFFFull) continue;
                w = v | 1;
                while (w >>= 1) ++idx;                 /* BSR, the slow obvious way */
                want = idx / SH[d] + 1;
                got  = SH[d] == 4 ? (idx >> 2) + 1
                     : SH[d] == 1 ? idx + 1
                     : (unsigned)(((unsigned long long)idx * 0xAAABull) >> 17) + 1;
                if (want != got) {
                    printf("  the arithmetic digit count is wrong: value %llu, shift %u:"
                           " want %u got %u\n", v, SH[d], want, got);
                    bad = 1;
                }
            }
        }
    }
    /* the three WIDE tables the power-of-two bases emit from: one byte -> two hex characters, six
       bits -> two octal characters, one byte -> eight binary characters. Each is checked back
       against the definition it is supposed to satisfy, computed the slow obvious way, because
       change 267's first table construction was wrong and a wrong table here would be a wrong
       digit in every number that used it. */
    {
        const unsigned char* t2 = (const unsigned char*)wia_i2c_tables2();
        const unsigned short* hex2 = (const unsigned short*)t2;
        const unsigned short* oct2 = (const unsigned short*)(t2 + 512);
        const unsigned char* bin8 = t2 + 640;
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
                char want = (char)('0' + ((i >> (7 - b)) & 1));
                if ((char)bin8[i * 8 + b] != want) {
                    printf("  the binary-byte table is wrong at %d, bit %d\n", i, b); bad = 1;
                }
            }
    }

    if (!bad) printf("  0. the eight assembler-generated tables match their definitions\n");
    return bad;
}

int main(void)
{
    static const ULONG BASES[] = { 0, 2, 8, 10, 16 };
    ULONG v, base;
    unsigned bi;
    LONG len;
    int k;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F_I2C)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlIntegerToChar");
    if (!sys) { printf("no RtlIntegerToChar\n"); return 2; }
    printf("== CORRECTNESS: RtlIntegerToChar ==\n");
    if (check_tables()) return 1;

    /* 1. every base 0..40, at several values and lengths including negative ones */
    {
        long before = cases;
        for (base = 0; base <= 40; ++base) {
            one(0, base, 40);
            one(3735928559ul, base, 40);
            one(3735928559ul, base, 4);
            one(3735928559ul, base, 0);
            one(3735928559ul, base, -40);
            one(3735928559ul, base, -4);
        }
        printf("  1. every base 0..40, six lengths each: %ld\n", cases - before);
    }

    /* 2. EVERY LENGTH -40..+40 at every digit-count boundary of every base */
    {
        long before = cases;
        for (bi = 0; bi < 5; ++bi) {
            ULONG b = BASES[bi] ? BASES[bi] : 10;
            unsigned long long p = 1;
            while (p <= 0xFFFFFFFFull) {
                ULONG lo = (ULONG)(p ? p - 1 : 0), hi = (ULONG)p;
                for (len = -40; len <= 40; ++len) { one(lo, BASES[bi], len); one(hi, BASES[bi], len); }
                if (p > 0xFFFFFFFFull / b) break;
                p *= b;
            }
        }
        printf("  2. every power of every base, one either side, every length -40..40: %ld\n",
               cases - before);
    }

    /* 3. the lengths that cannot be negated.
     *
     * INT_MIN is the only negative length that is a REFUSAL rather than a field width, and it is
     * here. ITS NEIGHBOUR IS NOT, AND THAT IS DELIBERATE: length INT_MIN+1 is a field width of
     * 2147483647, and the live export duly starts writing two billion zeros -- the first draft of
     * this file included it and the harness died of an access violation before it printed a single
     * mismatch. The largest negative length a 256-byte buffer can hold is -256, section 6 sweeps to
     * -200, and RESULTS.md records the far ones as a measurement from probes/negative.c (which has
     * a guard page) rather than as a comparison this corpus can safely run.
     */
    {
        long before = cases;
        for (bi = 0; bi < 5; ++bi) {
            one(3735928559ul, BASES[bi], (LONG)0x80000000ul);
            one(0, BASES[bi], (LONG)0x80000000ul);
            one(0, BASES[bi], -1);
            one(0, BASES[bi], -2);
            one(0, BASES[bi], 1);
            one(0, BASES[bi], 2);
        }
        printf("  3. INT_MIN, which is the one negative length that refuses: %ld\n",
               cases - before);
    }

    /* 4. a dense sweep of the low values in every base, at three lengths */
    {
        long before = cases;
        for (v = 0; v < 60000; ++v)
            for (bi = 0; bi < 5; ++bi) { one(v, BASES[bi], 40); one(v, BASES[bi], -40); }
        printf("  4. every value 0..59999, five bases, room and padded: %ld\n", cases - before);
    }

    /* 5. the whole 32-bit range on a prime stride */
    {
        long before = cases;
        unsigned long long step = 262147;
        for (v = 0; ; ) {
            for (bi = 0; bi < 5; ++bi) one(v, BASES[bi], 40);
            if (v + step > 0xFFFFFFFFull) break;
            v = (ULONG)(v + step);
        }
        one(0xFFFFFFFFul, 10, 40);
        one(0xFFFFFFFFul, 10, -40);
        one(0xFFFFFFFFul, 2, -40);
        printf("  5. the whole 32-bit range on a prime stride: %ld\n", cases - before);
    }

    /* 6. every length 1..200 on a value that is one digit, and on one that is ten */
    {
        long before = cases;
        for (k = 1; k <= 200; ++k) { one(7, 10, k); one(7, 10, -k); one(3735928559ul, 10, k);
                                     one(3735928559ul, 10, -k); }
        printf("  6. every length 1..200, padded and not, short and long values: %ld\n",
               cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    printf("  the live export answered SUCCESS %ld (of which %ld were ZERO-PADDED by a negative\n"
           "  length), INVALID_PARAMETER %ld, BUFFER_OVERFLOW %ld\n", n_ok, n_pad, n_inval, n_over);
    if (n_ok < 1000 || n_inval < 100 || n_over < 100 || n_pad < 1000) {
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
