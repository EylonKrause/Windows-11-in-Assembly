// changes/076-rtlcrc64/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern unsigned long long wia_crc64(const void*, size_t, unsigned long long);
void wia_crc64_init(void);
typedef unsigned long long (WINAPI *fn)(const void*, size_t, unsigned long long);
static fn sys;
typedef struct { unsigned char* p; size_t n; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return wia_crc64(m->p,m->n,0); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)sys(m->p,m->n,0); }
#pragma optimize("", on)
int main(void){
    wia_crc64_init();
    HMODULE h=GetModuleHandleW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlCrc64");
    static const int L[]={8,32,64,256,1024,4096,65536};
    static const char* N[]={"8","32","64","256","1024","4096","65536"};
    enum{K=7}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ unsigned char* p=malloc(L[i]); for(int k=0;k<L[i];k++)p[k]=(unsigned char)(k*7+1);
        cx[i].p=p; cx[i].n=L[i]; cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("RtlCrc64  (wia slicing-by-8 vs ntdll)", cs, K, 200);
}
