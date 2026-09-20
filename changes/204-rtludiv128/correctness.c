// changes/204-rtludiv128/correctness.c
// Gate 1: wia_udiv128 must be indistinguishable from ntdll!RtlUdiv128, quotient AND remainder.
//
// Three-way: our assembly vs the transcribed-loop oracle vs the LIVE export on this PC.
//
// The interesting structure here is the overflow boundary at DividendHigh == Divisor. Below it the
// implementation issues a hardware `div`, which would raise #DE if the boundary were misplaced by
// even one; at or above it the implementation takes a two-instruction saturating path. So the
// corpus is weighted hard onto that boundary, and also onto Divisor == 0, which must return
// all-ones rather than faulting.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

extern unsigned __int64 wia_udiv128(unsigned __int64, unsigned __int64,
                                    unsigned __int64, unsigned __int64*);
unsigned __int64 ref_udiv128(unsigned __int64, unsigned __int64,
                            unsigned __int64, unsigned __int64*);

typedef unsigned __int64 (NTAPI *FN)(unsigned __int64, unsigned __int64,
                                     unsigned __int64, unsigned __int64*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0xA5A5A5A5A5A5A5A5ull

static int one(unsigned __int64 hi, unsigned __int64 lo, unsigned __int64 d){
    unsigned __int64 ra = POISON, rb = POISON, rc = POISON;
    unsigned __int64 qa = wia_udiv128(hi, lo, d, &ra);
    unsigned __int64 qb = ref_udiv128(hi, lo, d, &rb);
    unsigned __int64 qc = sys        (hi, lo, d, &rc);
    if (qa != qb || qa != qc) return 0;
    if (ra != rb || ra != rc) return 0;
    /* and again with a NULL remainder pointer: the quotient must not change */
    if (wia_udiv128(hi, lo, d, NULL) != qc) return 0;
    return 1;
}

static unsigned __int64 sd = 0x2042042042ull;
static unsigned __int64 rnd64(void){
    sd = sd * 6364136223846793005ull + 1442695040888963407ull;
    unsigned __int64 x = sd >> 16;
    sd = sd * 6364136223846793005ull + 1442695040888963407ull;
    return (x << 32) ^ (sd >> 16);
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    sys = (FN)GetProcAddress(h, "RtlUdiv128");
    if(!sys){ printf("CORRECTNESS: cannot resolve ntdll!RtlUdiv128\n"); return 1; }

    // ---- small exhaustive block: every (hi, lo, d) with all three below 40 ----
    for (unsigned __int64 hi = 0; hi < 40; ++hi)
        for (unsigned __int64 lo = 0; lo < 40; ++lo)
            for (unsigned __int64 d = 0; d < 40; ++d)
                CHECK(one(hi, lo, d), "exhaustive small block, divisor 0 included");

    // ---- the structured edges of the 64-bit range ----
    {
        static const unsigned __int64 V[] = {
            0, 1, 2, 3, 4, 7, 8, 15, 16, 255, 256, 0xFFFF, 0x10000,
            0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0x8000000000000001ull,
            0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFFFFFFFFFull,
            0x9E3779B97F4A7C15ull, 0x5555555555555555ull, 0xAAAAAAAAAAAAAAAAull,
            0x100000000ull, 0xFFFFFFFFull
        };
        enum { NV = sizeof(V)/sizeof(V[0]) };
        for (int a = 0; a < NV; ++a)
            for (int b = 0; b < NV; ++b)
                for (int c = 0; c < NV; ++c)
                    CHECK(one(V[a], V[b], V[c]), "structured edge triple");
    }

    // ---- The overflow boundary: hi exactly at, just below and just above the divisor ----
    // This is where the implementation chooses between a hardware div and the saturating path.
    // One off-by-one here is either a wrong answer or a #DE, so it is swept deliberately.
    {
        static const unsigned __int64 D[] = {
            1, 2, 3, 17, 255, 0xFFFF, 0x100000000ull, 0x7FFFFFFFFFFFFFFFull,
            0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull, 0x9E3779B97F4A7C15ull
        };
        enum { ND = sizeof(D)/sizeof(D[0]) };
        static const unsigned __int64 LO[] = {
            0, 1, 0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0x123456789ABCDEFull
        };
        enum { NL = sizeof(LO)/sizeof(LO[0]) };
        for (int i = 0; i < ND; ++i) {
            unsigned __int64 d = D[i];
            for (int k = 0; k < NL; ++k) {
                if (d >= 1) CHECK(one(d - 1, LO[k], d), "hi == d-1 (last safe div)");
                CHECK(one(d,     LO[k], d), "hi == d (first saturating case)");
                if (d != 0xFFFFFFFFFFFFFFFFull)
                    CHECK(one(d + 1, LO[k], d), "hi == d+1");
            }
        }
    }

    // ---- divisor 0 across a spread of dividends: must saturate, must not fault ----
    {
        static const unsigned __int64 V[] = { 0, 1, 0xFFFFFFFFFFFFFFFFull,
                                              0x8000000000000000ull, 0x123456789ABCDEFull };
        for (int a = 0; a < 5; ++a)
            for (int b = 0; b < 5; ++b)
                CHECK(one(V[a], V[b], 0), "divisor 0 must saturate, not fault");
    }

    // ---- randomized fuzz, weighted so both regions and the boundary are all hit hard ----
    for (int t = 0; t < 1500000; ++t) {
        unsigned __int64 hi = rnd64(), lo = rnd64(), d = rnd64();
        switch (t % 7) {
            case 0: d &= 0xFFFFFFFFull; break;          /* small divisor -> saturating region */
            case 1: hi &= 0xFFFFull;    break;          /* small high    -> the div region    */
            case 2: d &= 0xFFull;       break;
            case 3: hi = d;             break;          /* exactly on the boundary            */
            case 4: hi = d ? d - 1 : 0; break;          /* the last safe value                */
            case 5: d = 0;              break;          /* the no-fault requirement           */
            default: break;
        }
        CHECK(one(hi, lo, d), "fuzz");
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (RtlUdiv128 vs live ntdll + a transcription of the shipped "
           "64-iteration loop -- quotient AND remainder, and every case re-run with a NULL "
           "remainder pointer: exhaustive (hi,lo,d) all < 40 so divisor 0 is covered densely, "
           "23^3 structured edge triples, the overflow boundary swept at d-1 / d / d+1 for 11 "
           "divisors x 5 low halves (one off-by-one there is a wrong answer or a #DE), divisor 0 "
           "across a dividend spread, and 1.5M fuzz cases weighted onto both regions and the "
           "boundary)\n");
    return 0;
}
