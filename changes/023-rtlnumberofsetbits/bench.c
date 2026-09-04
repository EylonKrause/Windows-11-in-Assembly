#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern unsigned long wia_numsetbits(const RTL_BITMAP*);
typedef ULONG (WINAPI *fn)(const RTL_BITMAP*);
static fn sys;
static uint64_t op_ours(void*c){ return wia_numsetbits((RTL_BITMAP*)c); }
static uint64_t op_sys (void*c){ return sys((RTL_BITMAP*)c); }
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlNumberOfSetBits");
    static const int B[]={64,256,1024,8192,65536,1048576};
    static const char* N[]={"64","256","1Kb","8Kb","64Kb","1Mb"};
    enum{K=6}; static RTL_BITMAP bm[K]; static wia_case cs[K];
    unsigned long seed=1;
    for(int i=0;i<K;++i){ int nw=(B[i]+31)/32; unsigned long* buf=malloc((nw+2)*4);
        for(int k=0;k<nw+2;k++){seed=seed*1103515245u+12345u; buf[k]=seed;}
        bm[i].SizeOfBitMap=(unsigned long)B[i]; bm[i].Buffer=buf;
        cs[i].label=N[i]; cs[i].bytes=B[i]/8; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&bm[i]; }
    return wia_bench_compare("RtlNumberOfSetBits  (wia POPCNT vs ntdll)", cs, K, 200);
}
