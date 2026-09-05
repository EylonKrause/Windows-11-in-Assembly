// changes/077-strset/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern char* wia_strset(char*, int);
typedef char* (__cdecl *fn)(char*, int);
static fn sys;
typedef struct { char* s; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ return (uint64_t)(uintptr_t)wia_strset(((ctx_t*)c)->s,'X'); }
static uint64_t op_sys (void*c){ return (uint64_t)(uintptr_t)sys(((ctx_t*)c)->s,'X'); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"_strset");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ char* s=malloc(L[i]+1); for(int k=0;k<L[i];k++)s[k]=(char)('a'+(k%23)); s[L[i]]=0;
        cx[i].s=s; cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("_strset  (wia AVX2 broadcast-fill vs ucrtbase)", cs, K, 200);
}
