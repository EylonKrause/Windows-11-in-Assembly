/* changes/292-filetimetosystemtime/probes/magics.c
 *
 * Throwaway. Finds and EXHAUSTIVELY verifies every constant divide used by impl.asm's shortened
 * calendar, to the standard change 126 set: a magic is accepted because floor((u*M)>>s) was
 * compared against floor(u/d) at every u it can meet, not because a formula says it should work.
 *
 * FIRST ATTEMPT AND WHY IT WAS THROWN AWAY. The first run of this probe verified each magic only
 * over the operand set the surrounding algorithm can actually present -- 36 525 distinct values for
 * /11758980, 366 for /2141 -- and happily returned magics that are WRONG a little way outside it.
 * /11758980 came back as (u*1461)>>34, whose error term 1461*11758980 - 2^34 = 596 makes it exact
 * only up to u < 28 825 619, against an operand that is a 32-bit quantity. It was verified, it was
 * complete for this caller, and it was a trap for the next reader. Every search below is therefore
 * constrained to the operand's FULL natural range, and the magic is reported only if it is exact
 * over all of it.
 *
 *   d = 146097     u in [0, 45039575]   (N_1 = 4*(days+584694)+3, days <= 10675199)  EXHAUSTIVE
 *   d = 11758980   u in [0, 2^32)       (low 32 bits of a 64-bit product)            BOUNDARIES
 *   d = 2141       u in [0, 65535]      (N_3 mod 65536)                              EXHAUSTIVE
 *   d = 3600000 / 60000 / 1000   u in [0, 86399999]  (millisecond of day)            EXHAUSTIVE
 *   d = 10000      u in [0, 863999999999]  (rem, 100-ns units within a day)          BOUNDARIES
 *
 * BOUNDARIES is a proof, not a sample: floor(u/d) changes only at multiples of d and (u*M)>>s is
 * non-decreasing in u, so agreement at u = q*d and u = q*d - 1 for EVERY q in range forces
 * agreement at every u between them.
 *
 * cl /nologo /O2 magics.c
 */
#include <stdio.h>
#include <stdint.h>

#ifdef _MSC_VER
#include <intrin.h>
static uint64_t mulhi(uint64_t a, uint64_t b) { uint64_t h; _umul128(a, b, &h); return h; }
#else
static uint64_t mulhi(uint64_t a, uint64_t b) { return (uint64_t)(((unsigned __int128)a * b) >> 64); }
#endif

/* Form used by the asm: imul r64, r64, imm32  then  shr. Needs M <= 0x7FFFFFFF and u*M < 2^63. */
static void find_imm32(const char* name, uint64_t d, uint64_t umax, int exhaustive)
{
    int s;
    for (s = 1; s <= 62; ++s) {
        uint64_t M = (((uint64_t)1 << s) / d) + 1;         /* the round-up magic */
        uint64_t u, q, ok = 1;
        if (M > 0x7FFFFFFFull) break;
        if (M && umax > 0x7FFFFFFFFFFFFFFFull / M) break;  /* the product would not fit */
        if (exhaustive) {
            for (u = 0; u <= umax; ++u)
                if (((u * M) >> s) != u / d) { ok = 0; break; }
        } else {
            for (q = 0; q <= umax / d; ++q) {
                u = q * d;
                if (((u * M) >> s) != q) { ok = 0; break; }
                if (u && (((u - 1) * M) >> s) != q - 1) { ok = 0; break; }
            }
            if (ok && ((umax * M) >> s) != umax / d) ok = 0;
        }
        if (ok) {
            printf("  %-16s /%-10llu  imul r64,r64,%-10llu  shr %-2d   %s over u in [0,%llu]\n",
                   name, (unsigned long long)d, (unsigned long long)M, s,
                   exhaustive ? "EXHAUSTIVE" : "every quotient boundary",
                   (unsigned long long)umax);
            return;
        }
    }
    printf("  %-16s /%llu  NO imm32 magic exact over [0,%llu]\n",
           name, (unsigned long long)d, (unsigned long long)umax);
}

/* Form used by the asm for the one wide divide: mulx (full 128-bit product) then shr. */
static void find_mulx(const char* name, uint64_t d, uint64_t umax)
{
    int s;
    for (s = 0; s <= 40; ++s) {
        uint64_t q64 = 0xFFFFFFFFFFFFFFFFull / d;
        uint64_t r64 = 0xFFFFFFFFFFFFFFFFull % d + 1;
        uint64_t M, q, u, ok = 1;
        if (r64 == d) { r64 = 0; q64 += 1; }
        if (s && q64 > (0xFFFFFFFFFFFFFFFFull >> s)) break;
        M = (q64 << s) + ((r64 << s) / d) + (((r64 << s) % d) != 0);   /* ceil(2^(64+s)/d) */
        for (q = 0; q <= umax / d; ++q) {
            u = q * d;
            if ((mulhi(u, M) >> s) != q) { ok = 0; break; }
            if (u && (mulhi(u - 1, M) >> s) != q - 1) { ok = 0; break; }
        }
        if (ok && (mulhi(umax, M) >> s) != umax / d) ok = 0;
        if (ok) {
            printf("  %-16s /%-10llu  mulx 0x%016llX          shr %-2d   every quotient boundary "
                   "over u in [0,%llu]\n", name, (unsigned long long)d, (unsigned long long)M, s,
                   (unsigned long long)umax);
            return;
        }
    }
    printf("  %-16s /%llu  NO mulx magic\n", name, (unsigned long long)d);
}

/* ---- verify the constants impl.asm ACTUALLY contains, in the exact form it uses them ---------- */

static void shown(const char* what, int ok, const char* how)
{ printf("  %-34s %s   %s\n", what, ok ? "OK  " : "FAIL", how); }

static void verify_shipped(void)
{
    uint64_t u, q, ok;

    /* days = mulhi(T, 0xA2E3FF1DE20581E3) >> 39, T in [0, 2^63).  Change 126's constant, re-proved
       here at every one of the 10 675 200 quotient boundaries in the domain plus both endpoints. */
    ok = 1;
    for (q = 0; q <= 10675199ull; ++q) {
        u = q * 864000000000ull;
        if ((mulhi(u, 0xA2E3FF1DE20581E3ull) >> 39) != q) { ok = 0; break; }
        if (u && (mulhi(u - 1, 0xA2E3FF1DE20581E3ull) >> 39) != q - 1) { ok = 0; break; }
    }
    if (ok && (mulhi(0x7FFFFFFFFFFFFFFFull, 0xA2E3FF1DE20581E3ull) >> 39) != 10675199ull) ok = 0;
    shown("days   = mulhi(T,0xA2E3..E3)>>39", (int)ok, "every quotient boundary, T in [0,2^63)");

    /* msday = mulhi(rem, 0x68DB8BAC710CC), rem in [0, 863999999999]. */
    ok = 1;
    for (q = 0; q <= 86399999ull; ++q) {
        u = q * 10000ull;
        if (mulhi(u, 0x68DB8BAC710CCull) != q) { ok = 0; break; }
        if (u && mulhi(u - 1, 0x68DB8BAC710CCull) != q - 1) { ok = 0; break; }
    }
    shown("msday  = mulhi(rem,0x68DB..CC)", (int)ok, "every quotient boundary, rem in [0,864e9)");

    /* weekday = (days+1) - 7*(((days+1)*0x24924925)>>32).  Change 126's constant. */
    ok = 1;
    for (u = 1; u <= 10675200ull; ++u) {
        uint64_t k = (u * 0x24924925ull) >> 32;
        if (u - 7 * k != u % 7) { ok = 0; break; }
    }
    shown("weekday= n - 7*((n*0x24924925)>>32)", (int)ok, "EXHAUSTIVE, n in [1,10675200]");

    /* C = (N_1*15051803)>>41 over every N_1 the calendar can produce. */
    ok = 1;
    for (u = 0; u <= 45039575ull; ++u)
        if (((u * 15051803ull) >> 41) != u / 146097ull) { ok = 0; break; }
    shown("C      = (N_1*15051803)>>41", (int)ok, "EXHAUSTIVE, N_1 in [0,45039575]");

    /* N_Y = (low32(P_2)*1531969483)>>54, over the whole 32-bit operand range. */
    ok = 1;
    for (q = 0; q <= 0xFFFFFFFFull / 11758980ull; ++q) {
        u = q * 11758980ull;
        if (((u * 1531969483ull) >> 54) != q) { ok = 0; break; }
        if (u && (((u - 1) * 1531969483ull) >> 54) != q - 1) { ok = 0; break; }
    }
    if (ok && ((0xFFFFFFFFull * 1531969483ull) >> 54) != 0xFFFFFFFFull / 11758980ull) ok = 0;
    shown("N_Y    = (low32*1531969483)>>54", (int)ok, "every quotient boundary, u in [0,2^32)");

    ok = 1;
    for (u = 0; u <= 65535ull; ++u)
        if (((u * 31345ull) >> 26) != u / 2141ull) { ok = 0; break; }
    shown("D      = ((N_3&0xFFFF)*31345)>>26", (int)ok, "EXHAUSTIVE, u in [0,65535]");

    ok = 1;
    for (u = 0; u <= 86399999ull; ++u) {
        if (((u * 39093747ull) >> 47) != u / 3600000ull) { ok = 0; break; }
        if (((u * 9162597ull)  >> 39) != u / 60000ull)   { ok = 0; break; }
        if (((u * 68719477ull) >> 36) != u / 1000ull)    { ok = 0; break; }
    }
    shown("hour/min/sec divides of msday", (int)ok, "EXHAUSTIVE, msday in [0,86399999]");
}

int main(void)
{
    printf("search (constrained to each operand's FULL natural range):\n");
    find_imm32("century",     146097,    45039575ull,   1);   /* N_1 max = 4*11259893+3 */
    find_imm32("year-in-cent",11758980,  0xFFFFFFFFull, 0);   /* low 32 bits of a product */
    find_imm32("day-in-month",2141,      65535ull,      1);   /* N_3 mod 65536            */
    find_mulx ("ms of day",   10000,     863999999999ull);    /* rem, 100-ns units        */
    find_imm32("hour",        3600000,   86399999ull,   1);
    find_imm32("minute",      60000,     86399999ull,   1);
    find_imm32("second",      1000,      86399999ull,   1);
    printf("\nverification of the constants impl.asm actually contains:\n");
    verify_shipped();
    return 0;
}
