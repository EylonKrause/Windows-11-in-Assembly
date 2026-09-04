#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern size_t wia_cmpmemulong(const void*, size_t, unsigned long);
typedef SIZE_T (WINAPI *fn)(const void*, SIZE_T, ULONG);
static fn sys;
typedef struct { const void* p; size_t n; unsigned long pat; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)wia_cmpmemulong(m->p,m->n,m->pat); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)sys(m->p,m->n,m->pat); }
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlCompareMemoryUlong");
    static const int B[]={16,64,256,4096,65536,1048576};
    static const char* N[]={"16","64","256","4KB","64KB","1MB"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    unsigned long pat=0x12345678;
    for(int i=0;i<K;++i){ unsigned long* b=malloc(B[i]); for(int k=0;k<B[i]/4;k++)b[k]=pat;
        cx[i].p=b; cx[i].n=B[i]; cx[i].pat=pat; cs[i].label=N[i]; cs[i].bytes=B[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("RtlCompareMemoryUlong  (wia AVX2 vs ntdll, all match)", cs, K, 200);
}
