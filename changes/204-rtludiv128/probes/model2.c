/* changes/204-rtludiv128/probes/model2.c
   Settle the hi >= d region properly.

   Two wrong guesses preceded this, and both looked plausible on a badly-chosen corpus:
     A. "the quotient saturates to all-ones, remainder = lo + d" -- true on the handful of cases the
        first probe tried, false in general;
     B. a 128-iteration long division -- which double-counts, because the shipped loop runs only 64
        iterations with the remainder register PRE-LOADED with hi.

   The real rule, read off the mismatch table: the result is the TRUE 128/64 quotient reduced mod
   2^64, with the TRUE remainder. That is computable with two hardware divisions and no loop:

        q1 = hi / d,  r1 = hi % d          (64/64, always safe)
        q0 = (r1:lo) / d,  r0 = (r1:lo) % d   (safe because r1 < d)
        Q = q1*2^64 + q0   =>   Q mod 2^64 = q0,   remainder = r0

   and when hi < d the first division yields q1 = 0, r1 = hi, so the second one alone is the whole
   answer -- which is why the fast path can skip straight to it.

   d == 0 is the one case that still needs its own answer, since `div` cannot be used at all. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef unsigned __int64 u64;
typedef u64 (NTAPI *FN)(u64,u64,u64,u64*);
static FN sys;

/* the two-division model, in C, using a compiler intrinsic for the 128/64 step */
extern u64 _udiv128(u64 highdividend, u64 lowdividend, u64 divisor, u64* remainder);

static u64 model(u64 hi, u64 lo, u64 d, u64* rem){
    if (d == 0) { if (rem) *rem = lo; return ~(u64)0; }   /* measured separately below */
    u64 r1;
    u64 q1 = _udiv128(0, hi, d, &r1);                     /* hi / d  -- 64/64, safe    */
    (void)q1;
    u64 r0;
    u64 q0 = _udiv128(r1, lo, d, &r0);                    /* r1 < d, so this cannot #DE */
    if (rem) *rem = r0;
    return q0;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    sys = (FN)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlUdiv128");
    if(!sys){ printf("no export\n"); return 1; }

    printf("=== d == 0: what does the live export return? ===\n");
    {
        static const u64 V[] = {0,1,255,0xFFFFFFFFFFFFFFFFull,0x8000000000000000ull,
                                0x123456789ABCDEFull};
        for(int a=0;a<6;a++) for(int b=0;b<6;b++){
            u64 r=0xA5A5A5A5A5A5A5A5ull;
            u64 q=sys(V[a],V[b],0,&r);
            if(a==0||b<2)
                printf("  hi=%016llX lo=%016llX -> q=%016llX r=%016llX  (r == lo? %s)\n",
                       V[a],V[b],q,r, (r==V[b])?"yes":"NO");
        }
    }

    printf("\n=== the two-division model vs the live export ===\n");
    {
        /* structured edges */
        static const u64 V[] = { 0,1,2,3,4,7,8,15,16,255,256,0xFFFFull,0x10000ull,
            0x7FFFFFFFFFFFFFFFull,0x8000000000000000ull,0x8000000000000001ull,
            0xFFFFFFFFFFFFFFFEull,0xFFFFFFFFFFFFFFFFull,0x9E3779B97F4A7C15ull,
            0x5555555555555555ull,0xAAAAAAAAAAAAAAAAull,0x100000000ull,0xFFFFFFFFull };
        enum { NV = sizeof(V)/sizeof(V[0]) };
        int bad=0, shown=0;
        for(int a=0;a<NV;a++)for(int b=0;b<NV;b++)for(int c=0;c<NV;c++){
            u64 hi=V[a],lo=V[b],d=V[c];
            u64 r1=0,r2=0;
            u64 q1=model(hi,lo,d,&r1), q2=sys(hi,lo,d,&r2);
            if(q1!=q2||r1!=r2){
                ++bad;
                if(shown<10){ printf("  MISMATCH hi=%016llX lo=%016llX d=%016llX  model %016llX/%016llX  live %016llX/%016llX\n",
                                     hi,lo,d,q1,r1,q2,r2); ++shown; }
            }
        }
        printf("  %d structured triples: %d mismatches\n", NV*NV*NV, bad);

        u64 s=424242ull; int bad2=0;
        for(int t=0;t<1000000;t++){
            s=s*6364136223846793005ull+1442695040888963407ull; u64 hi=s>>11;
            s=s*6364136223846793005ull+1442695040888963407ull; u64 lo=s>>11;
            s=s*6364136223846793005ull+1442695040888963407ull; u64 d=s>>11;
            switch(t%7){
                case 0: d&=0xFFFFFFFFull; break;
                case 1: hi&=0xFFFFull;    break;
                case 2: d&=0xFFull;       break;
                case 3: hi=d;             break;
                case 4: hi=d?d-1:0;       break;
                case 5: d=0;              break;
                default: break;
            }
            u64 r1=0,r2=0;
            if(model(hi,lo,d,&r1)!=sys(hi,lo,d,&r2) || r1!=r2) ++bad2;
        }
        printf("  1M fuzz cases (weighted onto the boundary and d==0): %d mismatches\n", bad2);
        if(!bad && !bad2) printf("\nTHE TWO-DIVISION MODEL IS EXACT.\n");
    }
    return 0;
}
