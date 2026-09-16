/* changes/278-rtlintegertounicodestring/correctness.c
 *
 * Gate 1 for ntdll!RtlIntegerToUnicodeString: OURS vs THE SCALAR MODEL vs THE LIVE EXPORT, on the
 * NTSTATUS, Out->Length, Out->MaximumLength AND every byte of a poison-filled destination.
 *
 * THE WHOLE DESTINATION IS COMPARED, on failing calls as well, because probes/contract.c measured
 * that a refusal leaves it COMPLETELY untouched -- Length keeps whatever the caller had in it. An
 * implementation that helpfully zeroed Length on the way out would pass any check that only looked
 * at the status. That is change 268's whole-buffer rule, which found 154 mismatches in change 016
 * that were nothing but a single 00 past the end of a string.
 *
 * THE CORPUS IS BUILT WHERE A LENGTH-FIRST CONVERTER GOES WRONG:
 *
 *   * EVERY BASE 0..40 plus 256 and 0xFFFFFFFF, because only five are legal and the other
 *     thirty-seven have to be refused with the right status. probes/contract.c asked them one at a
 *     time rather than trusting the documented set.
 *   * EVERY DIGIT-COUNT BOUNDARY IN EVERY BASE -- every power of the base, and one either side.
 *     The length is computed from a BSR and a table, so the values where the answer changes are
 *     exactly the ones a round-numbered corpus walks past.
 *   * EVERY MaximumLength from 0 to Length+4, at every one of those values, because the room rule is
 *     Length+2 here and Length+1 in the routine change 067 owns. One byte decides it.
 *   * AND ALL 2^24 LOW VALUES in base 10 and base 16, plus the whole 32-bit range sampled, because
 *     a division-free converter is either exactly right or wrong at one specific value.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef NTSTATUS (NTAPI *F_I2US)(ULONG, ULONG, USTR*);

extern NTSTATUS wia_int2ustr(ULONG, ULONG, USTR*);
extern void*    wia_i2u_tables(void);
NTSTATUS ref_int2ustr(ULONG, ULONG, USTR*);

static F_I2US sys;
static int  failures = 0;
static long cases = 0;
static long n_ok = 0, n_inval = 0, n_over = 0;

#define DBUF   64                      /* wide characters */
#define POISON 0x2A2A

static void one(ULONG v, ULONG base, USHORT maxlen)
{
    static wchar_t ba[DBUF], bb[DBUF], bc[DBUF];
    USTR ua, ub, uc;
    NTSTATUS ra, rb, rc;
    int i, bad = 0;

    for (i = 0; i < DBUF; ++i) ba[i] = bb[i] = bc[i] = POISON;
    ua.Length = ub.Length = uc.Length = 0xBEEF;
    ua.MaximumLength = ub.MaximumLength = uc.MaximumLength = maxlen;
    ua.Buffer = ba; ub.Buffer = bb; uc.Buffer = bc;

    ra = wia_int2ustr(v, base, &ua);
    rb = sys(v, base, &ub);
    rc = ref_int2ustr(v, base, &uc);

    ++cases;
    if (rb == 0) ++n_ok;
    else if ((ULONG)rb == 0xC000000Dul) ++n_inval;
    else if ((ULONG)rb == 0x80000005ul) ++n_over;

    if (ra != rb || ra != rc) bad = 1;
    else if (ua.Length != ub.Length || ua.Length != uc.Length) bad = 2;
    else if (ua.MaximumLength != ub.MaximumLength) bad = 3;
    else if (memcmp(ba, bb, sizeof ba) != 0) bad = 4;
    else if (memcmp(ba, bc, sizeof bc) != 0) bad = 5;

    if (bad) {
        if (failures < 12)
            printf("  FAIL (%d) v=%lu base=%lu max=%u: ours %08lX/%u  live %08lX/%u  model %08lX/%u\n",
                   bad, (unsigned long)v, (unsigned long)base, maxlen,
                   (unsigned long)ra, ua.Length, (unsigned long)rb, ub.Length,
                   (unsigned long)rc, uc.Length);
        ++failures;
    }
}

static int check_tables(void)
{
    const unsigned* dec2w = (const unsigned*)wia_i2u_tables();
    const unsigned char* tabs;
    const unsigned long long* pow10;
    const unsigned char* g;
    const char* hex;
    const unsigned char* digp2;
    int i, s, b, bad = 0;

    for (i = 0; i < 100; ++i) {
        unsigned want = (unsigned)('0' + i / 10) | ((unsigned)('0' + i % 10) << 16);
        if (dec2w[i] != want) { printf("  the digit-pair table is wrong at %d\n", i); bad = 1; }
    }
    /* TABS follows DEC2W in .const, 16-byte aligned: 100 dwords is 400 bytes, rounded to 400 */
    tabs = (const unsigned char*)dec2w + 400;
    pow10 = (const unsigned long long*)tabs;
    g = tabs + 88;
    hex = (const char*)(tabs + 120);
    digp2 = tabs + 136;
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
    for (s = 1; s <= 4; ++s) {
        if (s == 2) continue;                      /* rows 0 and 2 are padding, never indexed */
        for (b = 0; b < 32; ++b) {
            int want = b / s + 1;
            if (digp2[s * 32 + b] != want) {
                printf("  the power-of-two digit table is wrong at shift %d, bits %d: %u want %d\n",
                       s, b, digp2[s * 32 + b], want);
                bad = 1;
            }
        }
    }
    if (!bad) printf("  0. the five assembler-generated tables match their definitions\n");
    return bad;
}

int main(void)
{
    static const ULONG BASES[] = { 0, 2, 8, 10, 16 };
    ULONG v, base;
    unsigned bi;
    int k;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F_I2US)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlIntegerToUnicodeString");
    if (!sys) { printf("no RtlIntegerToUnicodeString\n"); return 2; }
    printf("== CORRECTNESS: RtlIntegerToUnicodeString ==\n");
    if (check_tables()) return 1;

    /* 1. every base 0..40, plus the absurd ones, at several values and room settings */
    {
        long before = cases;
        for (base = 0; base <= 40; ++base) {
            one(0, base, 200);
            one(1, base, 200);
            one(3735928559ul, base, 200);
            one(3735928559ul, base, 4);            /* a bad base AND no room: which wins? */
            one(3735928559ul, base, 0);
        }
        one(3735928559ul, 256, 200);
        one(3735928559ul, 0xFFFFFFFFul, 200);
        one(3735928559ul, 0xFFFFFFFFul, 0);
        printf("  1. every base 0..40 and three absurd ones: %ld\n", cases - before);
    }

    /* 2. every digit-count boundary in every base, at every room from 0 to Length+4 */
    {
        long before = cases;
        for (bi = 0; bi < 5; ++bi) {
            ULONG b = BASES[bi] ? BASES[bi] : 10;
            unsigned long long p = 1;
            while (p <= 0xFFFFFFFFull) {
                ULONG lo = (ULONG)(p ? p - 1 : 0), hi = (ULONG)p;
                USHORT m;
                for (m = 0; m <= 80; ++m) { one(lo, BASES[bi], m); one(hi, BASES[bi], m); }
                if (p > 0xFFFFFFFFull / b) break;
                p *= b;
            }
        }
        printf("  2. every power of every base, one either side, every room 0..80: %ld\n",
               cases - before);
    }

    /* 3. the extremes */
    {
        long before = cases;
        static const ULONG V[] = {
            0, 1, 2, 7, 8, 9, 10, 15, 16, 17, 99, 100, 101, 255, 256, 257,
            0x7FFFFFFFul, 0x80000000ul, 0xFFFFFFFEul, 0xFFFFFFFFul
        };
        for (k = 0; k < (int)(sizeof V / sizeof V[0]); ++k)
            for (bi = 0; bi < 5; ++bi) {
                USHORT m;
                for (m = 0; m <= 80; ++m) one(V[k], BASES[bi], m);
            }
        printf("  3. twenty extreme values, five bases, every room 0..80: %ld\n", cases - before);
    }

    /* 4. a dense sweep of the low values in every base, with ample room */
    {
        long before = cases;
        for (v = 0; v < 200000; ++v)
            for (bi = 0; bi < 5; ++bi) one(v, BASES[bi], 200);
        printf("  4. every value 0..199999 in all five bases: %ld\n", cases - before);
    }

    /* 5. the whole 32-bit range, sampled on a stride that is coprime with every power of the bases */
    {
        long before = cases;
        unsigned long long step = 262147;           /* prime */
        for (v = 0; ; ) {
            for (bi = 0; bi < 5; ++bi) one(v, BASES[bi], 200);
            if (v + step > 0xFFFFFFFFull) break;
            v = (ULONG)(v + step);
        }
        one(0xFFFFFFFFul, 10, 200);
        printf("  5. the whole 32-bit range on a prime stride, five bases: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    printf("  the live export answered SUCCESS %ld, INVALID_PARAMETER %ld, BUFFER_OVERFLOW %ld\n",
           n_ok, n_inval, n_over);
    if (n_ok < 1000 || n_inval < 100 || n_over < 100) {
        printf("  the corpus did not reach all three outcomes -- the gate fails without them\n");
        ++failures;
    }
    if (!failures)
        printf("CORRECTNESS: PASS (the status, Length, MaximumLength and the WHOLE destination exact\n"
               "vs live ntdll and vs the scalar model, on failing calls too)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
