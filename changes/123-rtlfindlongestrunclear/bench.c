// changes/123-rtlfindlongestrunclear/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern unsigned long wia_lrc(const RTL_BITMAP*, unsigned long*);
typedef ULONG (WINAPI *fn)(RTL_BITMAP*, PULONG);
static fn sys;
static uint64_t op_ours(void*c){ unsigned long s; return wia_lrc((RTL_BITMAP*)c,&s)^s; }
static uint64_t op_sys (void*c){ unsigned long s; return sys((RTL_BITMAP*)c,&s)^s; }
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlFindLongestRunClear");
    // (bits, density-mode): 0=sparse(long clear runs) 1=dense(short) 2=all-clear
    static const int B[]   ={256,4096,65536,4096,65536,65536};
    static const int MODE[]={  0,   0,    0,   1,    1,    2};
    static const char* N[] ={"256/sparse","4Kb/sparse","64Kb/sparse","4Kb/dense","64Kb/dense","64Kb/allclear"};
    enum{K=6}; static RTL_BITMAP bm[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ int nw=(B[i]+31)/32; unsigned long* buf=malloc((size_t)(nw+2)*4);
        unsigned long seed=0x9e37u+i*2654435761u;
        for(int k=0;k<nw+2;k++){ seed=seed*1103515245u+12345u; unsigned long w=seed;
            if(MODE[i]==0) w&=(seed>>11)&(seed>>19);   // very sparse set -> long clear runs
            else if(MODE[i]==1) w|=(seed<<5);          // dense set -> short clear runs
            else w=0;                                  // all clear
            buf[k]=w; }
        bm[i].SizeOfBitMap=(unsigned long)B[i]; bm[i].Buffer=buf;
        cs[i].label=N[i]; cs[i].bytes=B[i]/8; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&bm[i]; }
    return wia_bench_compare("RtlFindLongestRunClear  (wia word-scan+AVX2 vs ntdll)", cs, K, 200);
}
