// changes/130-rtlsetbits/correctness.c
// Bit-exact check of wia_setbits vs live ntdll!RtlSetBits + oracle: the ENTIRE resulting buffer must
// match byte-for-byte (so any stray write outside the requested range is caught), over an exhaustive
// small sweep of (start,num), pre-dirtied buffers, and 2M random cases.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern void wia_setbits(RTL_BITMAP*, unsigned long, unsigned long);
void ref_setbits(RTL_BITMAP*, unsigned long, unsigned long);
typedef void (WINAPI *fn)(RTL_BITMAP*, ULONG, ULONG);
static fn sys;
static int fails=0;
#define NW 96
static unsigned long b1[NW], b2[NW], b3[NW];
// guard words at the end catch writes past the region the test intends
static void chk(unsigned long n, unsigned long start, unsigned long num, unsigned long fill){
    for(int i=0;i<NW;i++){ b1[i]=fill; b2[i]=fill; b3[i]=fill; }
    RTL_BITMAP m1={n,b1}, m2={n,b2}, m3={n,b3};
    sys(&m1,start,num); wia_setbits(&m2,start,num); ref_setbits(&m3,start,num);
    if(memcmp(b1,b2,sizeof b1) || memcmp(b1,b3,sizeof b1)){
        if(fails<15){ printf("FAIL n=%lu start=%lu num=%lu fill=%08lX\n",n,start,num,fill);
            for(int i=0;i<8;i++) printf("   w%d sys=%08lX ours=%08lX ref=%08lX\n",i,b1[i],b2[i],b3[i]); }
        ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlSetBits");
    if(!sys){ printf("no RtlSetBits\n"); return 2; }
    unsigned long FILL[]={0x00000000ul,0xFFFFFFFFul,0xA5A5A5A5ul};
    // exhaustive small sweep: every start/num pair inside 300 bits, on 3 fill patterns
    for(int f=0; f<3 && fails<15; f++)
      for(unsigned long s=0; s<300 && fails<15; s++)
        for(unsigned long k=0; k<=300-s; k++) chk(1024,s,k,FILL[f]);
    // ranges crossing the 32-byte AVX boundary and long fills
    for(unsigned long s=0; s<70 && fails<15; s++)
      for(unsigned long k=0; k<600; k+=7) chk(2048,s,k,0xA5A5A5A5ul);
    // out-of-range: ntdll writes past SizeOfBitMap, so we must too
    chk(64,60,20,0); chk(64,100,10,0); chk(0,0,0,0); chk(0,5,10,0); chk(128,127,1,0xFFFFFFFFul);
    unsigned long seed=0x5e7b17u;
    for(int t=0;t<2000000 && fails<15;t++){
        seed=seed*1103515245u+12345u; unsigned long s=(seed>>7)%2000;
        seed=seed*1103515245u+12345u; unsigned long k=(seed>>7)%(3000-s);
        seed=seed*1103515245u+12345u; chk(2048,s,k,FILL[(seed>>5)%3]);
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlSetBits vs live + oracle, whole-buffer compare: exhaustive (start,num) sweep + AVX-boundary sweep + out-of-range + 2M fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
