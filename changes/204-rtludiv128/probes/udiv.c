/* changes/204-rtludiv128/probes/udiv.c
   Pin down ntdll!RtlUdiv128 before writing any assembly.

   THE SHIPPED CODE WAS READ FIRST (RVA 0x0014A250). It is a 64-iteration restoring shift-subtract
   long division, branch-free inside the loop but ALWAYS 64 passes, which is exactly why it measures
   a flat ~69 ns whatever the operands:

       rax = hi + hi;  r9 = lo >> 63;  rcx = lo + lo;  r9 |= rax   ; the shifted-in candidate
       rdx = hi sar 63                                             ; all ones if a bit shifted out
       r10 = r9 - r8;  rax = r9 | rdx                              ; saturate so the compare says >=
       rbx = rcx | 1
       cmp rax, r8;  cmovb r10, r9;  cmovb rbx, rcx                ; restore if candidate < divisor

   So: rcx = dividend high, rdx = dividend low, r8 = divisor, r9 = ULONG64* remainder (written only
   when non-NULL), quotient returned in rax. The quotient accumulates ONE BIT PER ITERATION into a
   64-bit register, so when the true quotient exceeds 2^64 the high bits are simply lost -- this
   probe measures exactly what comes back in that case rather than assuming.

   Three things must be settled before an implementation can use the hardware `div`, which does
   128/64 natively but raises #DE when the quotient will not fit in 64 bits:
     1. what is returned when hi >= divisor (the overflow region);
     2. what is returned when divisor == 0 (where `div` would fault outright);
     3. whether the remainder pointer may be NULL.                                                */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef ULONG64 (NTAPI *FN)(ULONG64 hi, ULONG64 lo, ULONG64 d, ULONG64* rem);
static FN f;

/* The reference: the shipped loop, transcribed. Used to confirm the reading is right. */
static ULONG64 ref_udiv128(ULONG64 hi, ULONG64 lo, ULONG64 d, ULONG64* rem){
    ULONG64 r = hi, q = lo;
    for (int i = 0; i < 64; ++i) {
        ULONG64 carry_out = r >> 63;                 /* the bit about to be shifted out of r   */
        ULONG64 cand = (r << 1) | (q >> 63);         /* shift the next dividend bit into r     */
        ULONG64 qn   = (q << 1) | 1;
        ULONG64 test = carry_out ? ~(ULONG64)0 : cand;   /* saturate: a bit left the top of r  */
        if (test >= d) { r = cand - d; q = qn; }
        else           { r = cand;     q = (q << 1); }
    }
    if (rem) *rem = r;
    return q;
}

static int fails = 0;
static void chk(ULONG64 hi, ULONG64 lo, ULONG64 d, const char* tag){
    ULONG64 r1 = 0xAAAAAAAAAAAAAAAAull, r2 = 0xAAAAAAAAAAAAAAAAull;
    ULONG64 q1 = f(hi, lo, d, &r1);
    ULONG64 q2 = ref_udiv128(hi, lo, d, &r2);
    if (q1 != q2 || r1 != r2) {
        if (fails < 20)
            printf("  MISMATCH %-22s hi=%016llX lo=%016llX d=%016llX\n"
                   "      live q=%016llX r=%016llX\n      ref  q=%016llX r=%016llX\n",
                   tag, hi, lo, d, q1, r1, q2, r2);
        ++fails;
    }
}

static unsigned long long sd = 0x204204ull;
static ULONG64 rnd64(void){
    sd = sd * 6364136223846793005ull + 1442695040888963407ull;
    ULONG64 x = sd >> 16;
    sd = sd * 6364136223846793005ull + 1442695040888963407ull;
    return (x << 32) ^ (sd >> 16);
}

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    f = (FN)GetProcAddress(h, "RtlUdiv128");
    if (!f) { printf("no RtlUdiv128 export\n"); return 1; }

    printf("=== 1. plain cases, hi < d (the region where one hardware div is exact) ===\n");
    {
        ULONG64 rem = 0;
        ULONG64 q = f(0, 1000, 7, &rem);
        printf("  1000 / 7            q=%llu r=%llu   (expect 142 r 6)\n", q, rem);
        q = f(0, 0xFFFFFFFFFFFFFFFFull, 3, &rem);
        printf("  (2^64-1) / 3        q=%016llX r=%llu\n", q, rem);
        q = f(1, 0, 3, &rem);
        printf("  2^64 / 3            q=%016llX r=%llu   (expect 5555555555555555 r 1)\n", q, rem);
    }

    printf("\n=== 2. the OVERFLOW region, hi >= d -- what does the 64-bit quotient become? ===\n");
    {
        ULONG64 rem = 0;
        /* true quotient = 2^64 exactly, which does not fit; the loop keeps the low 64 bits */
        ULONG64 q = f(1, 0, 1, &rem);
        printf("  hi=1 lo=0 d=1       q=%016llX r=%016llX   (true Q = 2^64)\n", q, rem);
        q = f(5, 0, 1, &rem);
        printf("  hi=5 lo=0 d=1       q=%016llX r=%016llX   (true Q = 5*2^64)\n", q, rem);
        q = f(3, 7, 2, &rem);
        printf("  hi=3 lo=7 d=2       q=%016llX r=%016llX\n", q, rem);
        q = f(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 1, &rem);
        printf("  all ones / 1        q=%016llX r=%016llX\n", q, rem);
        q = f(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 2, &rem);
        printf("  all ones / 2        q=%016llX r=%016llX\n", q, rem);
    }

    printf("\n=== 3. divisor == 0, where a hardware div would fault ===\n");
    {
        ULONG64 rem = 0xAAAAAAAAAAAAAAAAull;
        ULONG64 q = f(0, 1000, 0, &rem);
        printf("  1000 / 0            q=%016llX r=%016llX  (no fault: the loop just always subtracts)\n",
               q, rem);
        rem = 0xAAAAAAAAAAAAAAAAull;
        q = f(0, 0, 0, &rem);
        printf("  0 / 0               q=%016llX r=%016llX\n", q, rem);
    }

    printf("\n=== 4. NULL remainder pointer ===\n");
    {
        ULONG64 q = f(0, 1000, 7, NULL);
        printf("  1000 / 7, rem NULL  q=%llu   (the shipped code tests the pointer first)\n", q);
    }

    printf("\n=== 5. does the transcribed reference match the live export? ===\n");
    {
        /* structured edges */
        static const ULONG64 V[] = { 0, 1, 2, 3, 7, 0xFF, 0x100, 0x7FFFFFFFFFFFFFFFull,
                                     0x8000000000000000ull, 0xFFFFFFFFFFFFFFFEull,
                                     0xFFFFFFFFFFFFFFFFull, 0x9E3779B97F4A7C15ull };
        enum { NV = sizeof(V)/sizeof(V[0]) };
        for (int a = 0; a < NV; ++a)
            for (int b = 0; b < NV; ++b)
                for (int c = 0; c < NV; ++c)
                    chk(V[a], V[b], V[c], "edge triple");
        printf("  %d structured triples: %d mismatches\n", NV*NV*NV, fails);

        int before = fails;
        for (int t = 0; t < 400000; ++t) {
            ULONG64 hi = rnd64(), lo = rnd64(), d = rnd64();
            switch (t % 5) {
                case 0: d  &= 0xFFFFFFFFull; break;   /* small divisor -> overflow region */
                case 1: hi &= 0xFFFFull;     break;   /* small hi      -> the fast region */
                case 2: d  &= 0xFFull;       break;
                case 3: hi  = d;             break;   /* exactly on the boundary          */
                default: break;
            }
            chk(hi, lo, d, "fuzz");
        }
        printf("  400000 fuzz cases (weighted onto the overflow boundary): %d new mismatches\n",
               fails - before);
    }

    if (fails) { printf("\nCONTRACT NOT YET UNDERSTOOD: %d mismatches\n", fails); return 1; }
    printf("\nThe transcribed loop reproduces the live export EXACTLY over every case above.\n");
    return 0;
}
