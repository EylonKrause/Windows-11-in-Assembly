// changes/125-rtlfindclearbits/bench.c
// Realistic run-structured bitmaps (alternating clear/set regions of varied length, as heap/page
// allocation bitmaps actually look), searched for a clear run from a hint. See RESULTS.md for the
// adversarial random-dense worst case (where ntdll's per-byte table wins).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern unsigned long wia_findclearbits(const RTL_BITMAP*, unsigned long, unsigned long);
typedef ULONG (WINAPI *fn)(RTL_BITMAP*, ULONG, ULONG);
static fn sys;
typedef struct { RTL_BITMAP* bm; unsigned long num, hint; } CASE;
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; return wia_findclearbits(k->bm,k->num,k->hint); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; return sys((RTL_BITMAP*)k->bm,k->num,k->hint); }
// fill [0,nbits) with alternating clear/set runs; avg run length ~avg (bounded), mostly-allocated feel
static void fill_runs(unsigned long* buf, int nbits, unsigned long seed, int avg){
    int nw=(nbits+31)/32; for(int i=0;i<nw+2;i++)buf[i]=0;
    int pos=0, set=0;
    while(pos<nbits){ seed=seed*1103515245u+12345u; int run=1+((seed>>8)%(2*avg));
        int end=pos+run; if(end>nbits)end=nbits;
        if(set) for(int i=pos;i<end;i++) buf[i>>5]|=1u<<(i&31);
        pos=end; set^=1; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlFindClearBits");
    enum{K=6}; static RTL_BITMAP bm[K]; static CASE ca[K]; static wia_case cs[K];
    static const int   B[]   ={ 256, 1024, 8192, 65536, 65536, 262144};
    static const int   AVG[] ={   8,   16,   24,    32,    48,     64};
    static const ULONG NUM[] ={   5,   10,   20,    16,    40,     24};
    static const ULONG HINT[]={   0,    0,    0,  8000,     0,      0};
    static const char* N[]   ={"256/n5","1Kb/n10","8Kb/n20","64Kb/n16@8000","64Kb/n40","256Kb/n24"};
    for(int i=0;i<K;++i){ int nw=(B[i]+31)/32; unsigned long* buf=malloc((size_t)(nw+2)*4);
        fill_runs(buf,B[i],0x1000+i*2654435761u,AVG[i]);
        bm[i].SizeOfBitMap=(unsigned long)B[i]; bm[i].Buffer=buf;
        ca[i].bm=&bm[i]; ca[i].num=NUM[i]; ca[i].hint=HINT[i];
        cs[i].label=N[i]; cs[i].bytes=B[i]/8; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&ca[i]; }
    return wia_bench_compare("RtlFindClearBits  (wia word-scan+AVX2 vs ntdll)", cs, K, 200);
}
