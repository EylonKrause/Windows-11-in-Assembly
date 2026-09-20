/* changes/280-rtllargeintegertochar/probes/div64.c
 *
 * The reason this change was deferred out of 279, settled by exhaustive proof.
 *
 * Changes 067, 278 and 279 all rest on one identity: `(v * 0x51EB851F) >> 37 == v / 100`, which
 * change 067's probes/decimal.c verified by running it over ALL 4294967296 values of a 32-bit v.
 * That is a complete proof for a 32-bit domain and it says nothing about a 64-bit one. A 64-bit
 * value needs a 64-bit reciprocal, and there is no way to run 18446744073709551616 cases.
 *
 * So this change does not divide a 64-bit value by 100. It splits it into 32-bit pieces with ONE
 * 64-bit division, by 10^8:
 *
 *      q1 = v  / 10^8        r1 = v  - q1*10^8      the low  eight digits
 *      q2 = q1 / 10^8        r2 = q1 - q2*10^8      the next eight digits
 *      q2 < 1845                                     the top four digits, and it fits in 32 bits
 *
 * Eight plus eight plus four is twenty, which is exactly the longest decimal answer
 * (18446744073709551615). Every piece is then 32-bit, where 067's proved constant applies.
 *
 * The one 64-BIT constant is proved here, over all 2^64 values, without running 2^64 cases.
 *
 *      claim:   for all n in [0, 2^64),   umulh(n, M) >> 26  ==  n / 100000000
 *      with     M = 12379400392853802749 = ceil(2^90 / 10^8)
 *
 * Both sides are monotone non-decreasing in n. The right-hand side steps up by one exactly at each
 * multiple of d = 10^8 and nowhere else. So if the two agree at every step; that is, if
 * f(k*d) == k and f(k*d - 1) == k-1 for every k, then they agree everywhere in between, because
 * f is squeezed between two equal values. There are only 2^64 / 10^8 = 184467440737 such k, and
 * this probe checks every one of them. That is a complete proof of the identity over the whole
 * 64-bit domain, not a sample of it.
 *
 * It also prints the Granlund-Montgomery round-up criterion for the same constant, computed in
 * exact arithmetic, as an independent second opinion: with e = M*d - 2^90, the identity holds for
 * all n < 2^64 when e <= 2^26. Two arguments, one arithmetic and one exhaustive, that have to agree.
 *
 * AND the 64-bit digit-count machinery is checked the same way: digits10 over every power-of-ten
 * and bit-length boundary, and the power-of-two digit counts over all 64 bit lengths.
 *
 * Nothing here is used by the implementation at runtime. This file exists so that the constant in
 * impl.asm is a measured fact.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <intrin.h>

#define D    100000000ull
#define MAGIC 12379400392853802749ull
#define SHIFT 26

static unsigned long long div_fast(unsigned long long n)
{
    return __umulh(n, MAGIC) >> SHIFT;
}

/* ---- the exhaustive boundary sweep, split across the machine ---------------------------------- */

#define NTHREAD 16

typedef struct { unsigned long long k0, k1; long long bad; unsigned long long badk; } job_t;

static DWORD WINAPI worker(LPVOID arg)
{
    job_t* j = (job_t*)arg;
    unsigned long long k;
    for (k = j->k0; k < j->k1; ++k) {
        unsigned long long n = k * D;                 /* wraps only past the last k, excluded below */
        if (div_fast(n) != k || div_fast(n - 1) != k - 1) {
            j->bad = 1; j->badk = k; return 0;
        }
    }
    return 0;
}

int main(void)
{
    unsigned long long kmax = 0xFFFFFFFFFFFFFFFFull / D;   /* the last k with k*d <= 2^64-1 */
    HANDLE th[NTHREAD];
    job_t  jb[NTHREAD];
    int i, bad = 0;
    LARGE_INTEGER t0, t1, fq;

    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&fq);

    printf("== THE 64-BIT RECIPROCAL FOR 10^8 ==\n");
    printf("   claim:  umulh(n, %llu) >> %d  ==  n / %llu,  for all n < 2^64\n",
           MAGIC, SHIFT, D);

    /* ---- 1. the arithmetic criterion, in exact arithmetic -------------------------------------- */
    {
        /* e = M*d - 2^90, computed as a 128-bit product minus 2^90 without a big-int library.
           M*d = (hi:lo). 2^90 = 1 << 90, so its 128-bit form is hi = 2^26, lo = 0. */
        unsigned long long lo, hi, e_hi, e_lo;
        lo = MAGIC * D;
        hi = __umulh(MAGIC, D);
        e_hi = hi - (1ull << (SHIFT));               /* subtract 2^90 = (2^26) << 64 */
        e_lo = lo;                                    /* no borrow: lo of 2^90 is zero */
        printf("\n   1. the round-up criterion, exact:\n");
        printf("      M*d            = %llu * 2^64 + %llu\n", hi, lo);
        printf("      2^90           = %llu * 2^64 + 0\n", 1ull << SHIFT);
        printf("      e = M*d - 2^90 = %llu * 2^64 + %llu\n", e_hi, e_lo);
        printf("      2^%d            = %llu\n", SHIFT, 1ull << SHIFT);
        if (e_hi == 0 && e_lo <= (1ull << SHIFT))
            printf("      e <= 2^%d  ->  THE CRITERION HOLDS (M = ceil(2^90/d), so this is sufficient)\n",
                   SHIFT);
        else {
            printf("      e >  2^%d  ->  THE CRITERION FAILS\n", SHIFT);
            bad = 1;
        }
        /* and confirm M really is the round-up quotient: M*d - 2^90 must be in [1, d] */
        if (!(e_hi == 0 && e_lo >= 1 && e_lo <= D))
            { printf("      M is NOT ceil(2^90/d)\n"); bad = 1; }
        else
            printf("      1 <= e <= d, so M is exactly ceil(2^90/d)\n");
    }

    /* ---- 2. the exhaustive boundary sweep ------------------------------------------------------ */
    printf("\n   2. EVERY multiple of %llu below 2^64 -- %llu boundaries, each checked on both\n"
           "      sides, on %d threads. Agreement at every step of a monotone function is\n"
           "      agreement everywhere.\n", D, kmax, NTHREAD);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < NTHREAD; ++i) {
        jb[i].k0 = 1 + (unsigned long long)((double)kmax * i / NTHREAD);
        jb[i].k1 = 1 + (unsigned long long)((double)kmax * (i + 1) / NTHREAD);
        if (i == NTHREAD - 1) jb[i].k1 = kmax + 1;
        jb[i].bad = 0; jb[i].badk = 0;
        th[i] = CreateThread(0, 0, worker, &jb[i], 0, 0);
    }
    WaitForMultipleObjects(NTHREAD, th, TRUE, INFINITE);
    QueryPerformanceCounter(&t1);
    for (i = 0; i < NTHREAD; ++i) {
        CloseHandle(th[i]);
        if (jb[i].bad) { printf("      MISMATCH at k = %llu\n", jb[i].badk); bad = 1; }
    }
    printf("      %llu boundaries, %llu checks, %.1f s\n", kmax, kmax * 2,
           (double)(t1.QuadPart - t0.QuadPart) / fq.QuadPart);
    if (!bad) printf("      EXHAUSTIVE: the identity holds for every n in [0, 2^64)\n");

    /* the ends, spelled out */
    printf("      n = 0                    -> %llu (want 0)\n", div_fast(0));
    printf("      n = d-1                  -> %llu (want 0)\n", div_fast(D - 1));
    printf("      n = d                    -> %llu (want 1)\n", div_fast(D));
    printf("      n = 2^64-1               -> %llu (want %llu)\n",
           div_fast(0xFFFFFFFFFFFFFFFFull), 0xFFFFFFFFFFFFFFFFull / D);
    if (div_fast(0xFFFFFFFFFFFFFFFFull) != 0xFFFFFFFFFFFFFFFFull / D) bad = 1;

    /* ---- 3. the remainder, which is what the digits actually come from ------------------------- */
    printf("\n   3. the remainder r = n - q*d must be in [0, d) -- spot-checked at the ends and\n"
           "      swept over the top and bottom million\n");
    {
        unsigned long long n;
        long long r_bad = 0;
        for (n = 0; n < 1000000ull; ++n) {
            unsigned long long q = div_fast(n), r = n - q * D;
            if (r >= D || q * D + r != n) { ++r_bad; break; }
        }
        for (n = 0xFFFFFFFFFFFFFFFFull - 1000000ull; n != 0; ++n) {
            unsigned long long q = div_fast(n), r = n - q * D;
            if (r >= D || q * D + r != n) { ++r_bad; break; }
        }
        printf("      %s\n", r_bad ? "REMAINDER WRONG" : "remainder in range and q*d + r == n");
        if (r_bad) bad = 1;
    }

    /* ---- 4. the two-step split the implementation actually performs ---------------------------- */
    printf("\n   4. the split itself: q2 = (v/d)/d must be < 10000 so that it is four digits and\n"
           "      fits in 32 bits, for EVERY v -- which means checking it at v = 2^64-1 only,\n"
           "      since the whole chain is monotone\n");
    {
        unsigned long long v = 0xFFFFFFFFFFFFFFFFull;
        unsigned long long q1 = div_fast(v), r1 = v - q1 * D;
        unsigned long long q2 = div_fast(q1), r2 = q1 - q2 * D;
        printf("      v  = %llu\n      q1 = %llu  r1 = %08llu\n      q2 = %llu  r2 = %08llu\n",
               v, q1, r1, q2, r2);
        printf("      reassembled: %llu%08llu%08llu\n", q2, r2, r1);
        if (q2 >= 10000ull) { printf("      q2 DOES NOT FIT IN FOUR DIGITS\n"); bad = 1; }
        else printf("      q2 < 10000, so the top piece is at most four digits: 4 + 8 + 8 = 20\n");
    }

    /* ---- 5. digits10 for a 64-bit value -------------------------------------------------------- */
    printf("\n   5. digits10 from BSR and one compare, over every boundary that can change it\n");
    {
        static unsigned long long P10[21];
        unsigned char GT[64];
        int b, n_bad = 0;
        unsigned long long p = 1;
        for (b = 0; b <= 19; ++b) { P10[b] = p; if (b < 19) p *= 10; }
        for (b = 0; b < 64; ++b) GT[b] = (unsigned char)((b * 1233) / 4096 + 1);
        for (b = 0; b < 64; ++b) {
            unsigned long long lo = 1ull << b, t = 10;
            int want = 1;
            while (lo >= t) { ++want; if (want >= 20) break; t *= 10; }
            if (GT[b] != want) { printf("      GTAB[%d] = %u, want %d\n", b, GT[b], want); ++n_bad; }
        }
        /* and the rule digits = GTAB[bsr] + (v >= 10^GTAB[bsr]) at every power of ten +/- 1 */
        for (b = 0; b <= 19; ++b) {
            int e;
            for (e = -1; e <= 1; ++e) {
                unsigned long long v = P10[b] + e, t = 10;
                unsigned long idx; unsigned d_want = 1, d_got;
                if (b == 0 && e < 0) continue;
                while (v >= t) { ++d_want; if (d_want >= 20) break; if (t > 0xFFFFFFFFFFFFFFFFull/10) break; t *= 10; }
                _BitScanReverse64(&idx, v | 1);
                d_got = GT[idx];
                if (d_got <= 19 && v >= P10[d_got]) ++d_got;
                if (d_got != d_want) {
                    printf("      digits10(%llu) = %u, want %u\n", v, d_got, d_want); ++n_bad;
                }
            }
        }
        printf("      %s\n", n_bad ? "DIGIT COUNT WRONG" : "GTAB and the correction are exact at "
                                                           "every boundary");
        if (n_bad) bad = 1;
    }

    /* ---- 6. the power-of-two digit counts ------------------------------------------------------ */
    printf("\n   6. the power-of-two digit counts, arithmetic, over all 64 bit lengths\n");
    {
        int idx, n_bad = 0;
        for (idx = 0; idx < 64; ++idx) {
            unsigned got_h = (idx >> 2) + 1, want_h = idx / 4 + 1;
            unsigned got_o = (unsigned)(((unsigned long long)idx * 0xAAABull) >> 17) + 1;
            unsigned want_o = idx / 3 + 1;
            unsigned got_b = idx + 1, want_b = idx + 1;
            if (got_h != want_h) { printf("      hex at %d: %u vs %u\n", idx, got_h, want_h); ++n_bad; }
            if (got_o != want_o) { printf("      oct at %d: %u vs %u\n", idx, got_o, want_o); ++n_bad; }
            if (got_b != want_b) { ++n_bad; }
        }
        printf("      %s  (hex >>2, octal (n*0AAABh)>>17, binary n+1; longest answers "
               "16 / 22 / 64)\n", n_bad ? "WRONG" : "exact for every bit length 0..63");
        if (n_bad) bad = 1;
    }

    printf("\n%s\n", bad ? "DIV64: FAIL" :
           "DIV64: PASS -- the 64-bit reciprocal is proved over the WHOLE domain, both by the\n"
           "round-up criterion in exact arithmetic and by checking every one of the 184467440737\n"
           "boundaries where the quotient steps. The constant in impl.asm is a measured fact.");
    return bad ? 1 : 0;
}
