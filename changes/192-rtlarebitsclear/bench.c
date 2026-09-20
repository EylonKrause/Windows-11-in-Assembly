// changes/192-rtlarebitsclear/bench.c
// Gate 2: time wia_arebitsclear against the live ntdll!RtlAreBitsClear.
// The bitmap is all-clear so the scan runs to completion, the honest worst case for both
// sides, and the one that actually measures throughput rather than an early exit.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern unsigned char wia_arebitsclear(const RTL_BITMAP*, unsigned long, unsigned long);
typedef BOOLEAN (WINAPI *fn)(const RTL_BITMAP*, ULONG, ULONG);
static fn sys;
typedef struct { RTL_BITMAP bm; unsigned long start, len; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return wia_arebitsclear(&m->bm,m->start,m->len); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return sys(&m->bm,m->start,m->len); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlAreBitsClear");
    static const int Ln[]={32,128,1024,8192,65536,1000000};
    static const char* Nm[]={"32","128","1K","8K","64K","1M"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ int size=Ln[i]+64; int nw=(size+31)/32;
        unsigned long* b=(unsigned long*)malloc((size_t)(nw+2)*4);
        for(int k=0;k<nw+2;k++) b[k]=0;
        cx[i].bm.SizeOfBitMap=(unsigned long)size; cx[i].bm.Buffer=b;
        cx[i].start=1; cx[i].len=(unsigned long)Ln[i];   /* start=1 forces the partial-word path */
        cs[i].label=Nm[i]; cs[i].bytes=Ln[i]/8; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("RtlAreBitsClear  (wia AVX2 vptest vs ntdll, all clear)", cs, K, 200);
}
