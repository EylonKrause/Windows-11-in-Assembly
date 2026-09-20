/* changes/204-rtludiv128/probes/sat.c
   Characterise what the shipped loop actually returns in the hi >= d region.

   The first cut of impl.asm assumed the quotient saturates to all-ones there with remainder
   lo + d (mod 2^64). That held on every case the first probe happened to try and is WRONG in
   general -- the loop's 64-bit remainder register overflows, `shifted_out` then forces a subtract,
   and the quotient bits do not all come out set. This measures the real rule. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef unsigned __int64 u64;
typedef u64 (NTAPI *FN)(u64, u64, u64, u64*);
static FN sys;

static u64 ref_loop(u64 hi, u64 lo, u64 d, u64* rem){
    u64 r = hi, q = lo;
    for (int i = 0; i < 64; ++i) {
        u64 out  = r >> 63;
        u64 cand = (r << 1) | (q >> 63);
        u64 qs   = (q << 1) | 1;
        u64 test = out ? ~(u64)0 : cand;
        if (test >= d) { r = cand - d; q = qs; }
        else           { r = cand;     q = (q << 1); }
    }
    if (rem) *rem = r;
    return q;
}

/* candidate model A: saturate quotient, remainder = lo + d  (the wrong first cut) */
static u64 model_sat(u64 hi, u64 lo, u64 d, u64* rem){
    if (rem) *rem = lo + d;
    (void)hi; return ~(u64)0;
}

/* candidate model B: the TRUE wrapped quotient via two divisions, exact 128/64 long division.
   q_true = (hi:lo)/d, and we keep the low 64 bits; r_true = (hi:lo) mod d. */
static u64 model_wrap(u64 hi, u64 lo, u64 d, u64* rem){
    /* software long division, 128 bits, exact, used only as a model here */
    u64 q = 0, r = 0;
    for (int i = 127; i >= 0; --i) {
        u64 bit = (i >= 64) ? ((hi >> (i - 64)) & 1) : ((lo >> i) & 1);
        /* r = r*2 + bit, comparing against d with the overflow captured */
        u64 rhi = r >> 63;
        r = (r << 1) | bit;
        if (rhi || r >= d) { r -= d; if (i < 64) q |= (1ull << i); else q |= 0; }
    }
    if (rem) *rem = r;
    return q;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    sys = (FN)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlUdiv128");
    if(!sys){ printf("no export\n"); return 1; }

    /* first, confirm ref_loop == live, so the loop can be used as ground truth cheaply */
    {
        int bad = 0; u64 s = 99991ull;
        for (int t = 0; t < 200000; ++t) {
            s = s*6364136223846793005ull + 1442695040888963407ull; u64 hi = s >> 11;
            s = s*6364136223846793005ull + 1442695040888963407ull; u64 lo = s >> 11;
            s = s*6364136223846793005ull + 1442695040888963407ull; u64 d  = (s >> 40);
            u64 r1,r2;
            if (ref_loop(hi,lo,d,&r1) != sys(hi,lo,d,&r2) || r1 != r2) ++bad;
        }
        printf("ref_loop vs live over 200k: %d mismatches\n\n", bad);
    }

    printf("=== hi >= d : the first 25 cases where model A (saturate) is wrong ===\n");
    printf("%-18s %-18s %-18s | %-18s %-18s | %-18s %-18s\n",
           "hi","lo","d","loop q","loop r","A q","A r");
    {
        int shown = 0, tested = 0, badA = 0, badB = 0;
        u64 s = 1234567ull;
        for (int t = 0; t < 400000 && shown < 25; ++t) {
            s = s*6364136223846793005ull + 1442695040888963407ull; u64 d = (s >> 40) | 1;
            s = s*6364136223846793005ull + 1442695040888963407ull; u64 lo = s >> 11;
            s = s*6364136223846793005ull + 1442695040888963407ull; u64 hi = d + (s >> 40);
            if (hi < d) continue;                    /* only the saturating region */
            ++tested;
            u64 rl, ra, rb;
            u64 ql = ref_loop(hi,lo,d,&rl);
            u64 qa = model_sat(hi,lo,d,&ra);
            u64 qb = model_wrap(hi,lo,d,&rb);
            if (ql != qa || rl != ra) {
                ++badA;
                if (shown < 25) {
                    printf("%016llX %016llX %016llX | %016llX %016llX | %016llX %016llX\n",
                           hi,lo,d, ql,rl, qa,ra);
                    ++shown;
                }
            }
            if (ql != qb || rl != rb) ++badB;
        }
        printf("\nover %d saturating-region cases: model A wrong %d, model B (true wrap) wrong %d\n",
               tested, badA, badB);
    }

    /* Is the quotient always all-ones in that region? And is the remainder ever != lo+d? */
    {
        int qnotones = 0, rnotlod = 0, tested = 0;
        u64 s = 777ull;
        for (int t = 0; t < 300000; ++t) {
            s = s*6364136223846793005ull + 1442695040888963407ull; u64 d = (s >> 40) | 1;
            s = s*6364136223846793005ull + 1442695040888963407ull; u64 lo = s >> 11;
            s = s*6364136223846793005ull + 1442695040888963407ull; u64 hi = d + (s >> 40);
            if (hi < d) continue;
            ++tested;
            u64 rl; u64 ql = ref_loop(hi,lo,d,&rl);
            if (ql != ~(u64)0) ++qnotones;
            if (rl != (u64)(lo + d)) ++rnotlod;
        }
        printf("\nsaturating region, %d cases: quotient != all-ones in %d, remainder != lo+d in %d\n",
               tested, qnotones, rnotlod);
    }
    return 0;
}
