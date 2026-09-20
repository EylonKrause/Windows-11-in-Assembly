// changes/293-systemtimetofiletime/probes/magics.c
// Probe 3. Every constant divide in impl.asm is a multiply-high with a magic number. Verify each one
// EXHAUSTIVELY over the whole 16-bit input range (not just the legal year range), so no input -- valid
// or garbage -- can take a different branch than the oracle's true division.
//   (1461*yy) >> 2   == 365*yy + yy/4
//   (5243*yy) >> 19  == yy/100
//   (10486*yy) >> 22 == yy/400
// Also prints the doy table used by impl.asm and checks it against (153*mp+2)/5.
// Also links ntdll!RtlSetLastWin32ErrorAndNtStatusFromNtStatus to prove the import resolves.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

EXTERN_C NTSYSAPI void NTAPI RtlSetLastWin32ErrorAndNtStatusFromNtStatus(LONG);

int main(void){
    // First failure of each magic, searched from 0 upward: that is the exact validity bound. The
    // operand yy = Year - (Month<=2) is in [1600, 30827] for every input the validation lets through,
    // so a bound comfortably above 30827 is what has to be proved.
    uint32_t f1461=0, f5243=0, f10486=0;
    for(uint32_t y=0; y<=200000; ++y){
        if(!f1461  && (uint32_t)((1461u*y)>>2)  != 365u*y + y/4 ) f1461=y;
        if(!f5243  && (uint32_t)((5243u*y)>>19) != y/100        ) f5243=y;
        if(!f10486 && (uint32_t)((10486u*y)>>22)!= y/400        ) f10486=y;
    }
    printf("magic validity bounds (first y where the magic differs from true division):\n");
    printf("  (1461*y)>>2  == 365y + y/4 : first failure y=%u  (none below)\n", f1461);
    printf("  (5243*y)>>19 == y/100      : first failure y=%u\n", f5243);
    printf("  (10486*y)>>22== y/400      : first failure y=%u\n", f10486);
    int bad = (f5243 && f5243<=30827) || (f10486 && f10486<=30827) || (f1461 && f1461<=30827);
    printf("all three exact over the whole legal operand range yy in [1600,30827]: %s\n", bad?"NO":"YES");
    { int e=0; for(uint32_t y=0;y<=30827;++y){
        if((uint32_t)((1461u*y)>>2)!=365u*y+y/4) ++e;
        if((uint32_t)((5243u*y)>>19)!=y/100) ++e;
        if((uint32_t)((10486u*y)>>22)!=y/400) ++e; }
      printf("exhaustive recheck over [0,30827]: %d mismatches\n", e); bad |= e; }
    // overflow headroom of each 32-bit product at the top of the range
    printf("  1461*65535=%u  5243*65535=%u  10486*65535=%u   (2^31=%u)\n",
           1461u*65535u, 5243u*65535u, 10486u*65535u, 2147483648u);

    // doy table: index by MONTH directly, value = (153*mp+2)/5 with mp = m>2 ? m-3 : m+9, minus 584695
    printf("doytab (dword, = (153*mp+2)/5 - 584695):\n  ");
    for(int m=0;m<=12;++m){
        if(m==0){ printf("%d, ", 0); continue; }
        int mp = (m>2)? m-3 : m+9;
        int v  = (153*mp+2)/5;
        printf("%d, ", v-584695);
    }
    printf("\n  raw (153*mp+2)/5: ");
    for(int m=1;m<=12;++m){ int mp=(m>2)?m-3:m+9; printf("%d ", (153*mp+2)/5); }
    printf("\n");

    // the leap test used in the February path: leap(Y) == (Y & (Y%100 ? 3 : 15)) == 0
    int lbad=0;
    for(int Y=1601;Y<=30827;++Y){
        int truth = (Y%4==0 && Y%100!=0) || (Y%400==0);
        int mask  = (Y%100)? 3 : 15;
        int mine  = ((Y & mask)==0);
        if(truth!=mine){ if(lbad<5) printf("leap FAIL Y=%d\n",Y); ++lbad; }
    }
    printf("leap trick over Y in [1601,30827]: %s (%d failures)\n", lbad?"FAIL":"PASS", lbad);

    // full-formula check against a naive day counter, every day 1601-01-01 .. 30827-12-31
    {
        static const int DOY[13] = {0,306,337,0,31,61,92,122,153,184,214,245,275};
        static const int DIM[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        long long naive = 0; int fbad=0; long long n=0;
        for(int Y=1601; Y<=30827; ++Y){
            int leap = (Y%4==0 && Y%100!=0) || (Y%400==0);
            for(int m=1;m<=12;++m){
                int dim = DIM[m] + (m==2 && leap);
                for(int d=1; d<=dim; ++d){
                    unsigned yy = (unsigned)(Y - (m<=2));
                    long long E = (1461u*yy)>>2;
                    long long C = (5243u*yy)>>19;
                    long long D = (10486u*yy)>>22;
                    long long days = (long long)(unsigned)(E - C) + (long long)(D + (DOY[m]-584695) + d);
                    if(days != naive){ if(fbad<5) printf("FORMULA FAIL %d-%02d-%02d mine=%lld naive=%lld\n",Y,m,d,days,naive); ++fbad; }
                    ++naive; ++n;
                }
            }
        }
        printf("days-from-civil formula over all %lld days: %s (%d failures)\n", n, fbad?"FAIL":"PASS", fbad);
        printf("  last day index = %lld  -> t = %lld (+ 23:59:59.999)\n", naive-1,
               (naive-1)*864000000000LL + 23LL*36000000000LL + 59LL*600000000LL + 59LL*10000000LL + 999LL*10000LL);
    }

    // import resolves?
    { DWORD keep = GetLastError();
      RtlSetLastWin32ErrorAndNtStatusFromNtStatus((LONG)0xC000000D);
      printf("RtlSetLastWin32ErrorAndNtStatusFromNtStatus linked: GetLastError=%lu (want 87)\n",
             (unsigned long)GetLastError());
      SetLastError(keep); }
    return bad||lbad;
}
