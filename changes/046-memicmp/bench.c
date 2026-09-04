// changes/046-memicmp/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern int wia_memicmp(const void*, const void*, size_t);
typedef int (__cdecl *fn)(const void*, const void*, size_t);
static fn sys;
typedef struct { const void* a; const void* b; size_t n; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)wia_memicmp(m->a,m->b,m->n); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)sys(m->a,m->b,m->n); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"_memicmp");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        unsigned char* a=malloc(L[i]); unsigned char* b=malloc(L[i]);
        for(int k=0;k<L[i];k++){ a[k]=(unsigned char)('A'+(k%23)); b[k]=(unsigned char)('a'+(k%23)); } // ci-equal -> full n scan
        cx[i].a=a; cx[i].b=b; cx[i].n=(size_t)L[i];
        cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("_memicmp  (wia AVX2 in-reg ASCII fold vs ucrtbase, ci-equal, full n scan)", cs, K, 200);
}
