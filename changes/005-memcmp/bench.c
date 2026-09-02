// changes/005-memcmp/bench.c -- equal buffers (worst case), vs live ucrtbase memcmp.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern int wia_memcmp(const void*, const void*, size_t);
typedef int (__cdecl *fn)(const void*, const void*, size_t);
static fn sys;
typedef struct { const void* a; const void* b; size_t n; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)wia_memcmp(m->a,m->b,m->n); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)sys(m->a,m->b,m->n); }
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"memcmp");
    static const size_t L[]={8,32,128,1024,8192,65536,1048576};
    static const char* N[]={"8","32","128","1KB","8KB","64KB","1MB"};
    enum{K=7}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ unsigned char*a=(unsigned char*)malloc(L[i]),*b=(unsigned char*)malloc(L[i]); for(size_t k=0;k<L[i];++k)a[k]=b[k]=0x33; cx[i].a=a;cx[i].b=b;cx[i].n=L[i]; cs[i].label=N[i];cs[i].bytes=L[i];cs[i].ours=op_ours;cs[i].system=op_sys;cs[i].ctx=&cx[i]; }
    return wia_bench_compare("memcmp  (wia AVX2 vs ucrtbase, equal buffers)", cs, K, 200);
}
