// changes/043-stricmp/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern int wia_stricmp(const char*, const char*);
typedef int (__cdecl *fn)(const char*, const char*);
static fn sys;
typedef struct { const char* a; const char* b; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)wia_stricmp(m->a,m->b); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)sys(m->a,m->b); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"_stricmp");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        char* a=malloc(L[i]+1); char* b=malloc(L[i]+1);
        for(int k=0;k<L[i];k++){ a[k]=(char)('A'+(k%23)); b[k]=(char)('a'+(k%23)); }
        a[L[i]]=b[L[i]]=0;
        cx[i].a=a; cx[i].b=b;
        cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("_stricmp  (wia AVX2 in-reg ASCII fold vs ucrtbase, case-equal, full scan)", cs, K, 200);
}
