#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern int wia_strcmp(const char*, const char*);
typedef int (__cdecl *fn)(const char*, const char*);
static fn sys;
typedef struct { const char* a; const char* b; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)wia_strcmp(m->a,m->b); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)sys(m->a,m->b); }
static char* mk(size_t n){ char* s=malloc(n+1); for(size_t i=0;i<n;i++)s[i]=(char)('a'+(i&15)); s[n]=0; return s; }
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"strcmp");
    static const size_t L[]={3,15,63,255,1023,8191};
    static const char* N[]={"3","15","63","255","1023","8191"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ cx[i].a=mk(L[i]); cx[i].b=mk(L[i]); cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("strcmp  (wia AVX2 vs ucrtbase, equal)", cs, K, 200);
}
