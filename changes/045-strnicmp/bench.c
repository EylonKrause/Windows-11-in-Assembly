// changes/045-strnicmp/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern int wia_strnicmp(const char*, const char*, size_t);
typedef int (__cdecl *fn)(const char*, const char*, size_t);
static fn sys;
typedef struct { const char* a; const char* b; size_t n; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)wia_strnicmp(m->a,m->b,m->n); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)sys(m->a,m->b,m->n); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"_strnicmp");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        char* a=malloc(L[i]+1); char* b=malloc(L[i]+1);
        for(int k=0;k<L[i];k++){ a[k]=(char)('A'+(k%23)); b[k]=(char)('a'+(k%23)); }
        a[L[i]]=b[L[i]]=0;
        cx[i].a=a; cx[i].b=b; cx[i].n=(size_t)L[i];
        cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("_strnicmp  (wia AVX2 in-reg ASCII fold vs ucrtbase, case-equal, full n scan)", cs, K, 200);
}
