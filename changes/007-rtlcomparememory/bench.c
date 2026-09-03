// changes/007-rtlcomparememory/bench.c -- equal buffers (worst case), vs live ntdll RtlCompareMemory.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern size_t wia_rtlcmpmem(const void*,const void*,size_t);
typedef SIZE_T (WINAPI *fn)(const void*,const void*,SIZE_T);
static fn sys;
typedef struct{ const void*a; const void*b; size_t n; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)wia_rtlcmpmem(m->a,m->b,m->n); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)sys(m->a,m->b,m->n); }
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlCompareMemory");
    static const size_t L[]={8,32,128,1024,8192,65536,1048576};
    static const char* N[]={"8","32","128","1KB","8KB","64KB","1MB"};
    enum{K=7}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ unsigned char*a=malloc(L[i]),*b=malloc(L[i]); for(size_t k=0;k<L[i];k++)a[k]=b[k]=(unsigned char)(k*97+3); cx[i].a=a;cx[i].b=b;cx[i].n=L[i]; cs[i].label=N[i];cs[i].bytes=L[i];cs[i].ours=op_ours;cs[i].system=op_sys;cs[i].ctx=&cx[i]; }
    return wia_bench_compare("RtlCompareMemory  (wia AVX2 vs ntdll)", cs, K, 200);
}
