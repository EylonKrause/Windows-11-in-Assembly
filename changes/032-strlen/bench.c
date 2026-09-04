#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern size_t wia_strlen(const char*);
typedef size_t (__cdecl *fn)(const char*);
static fn sys;
typedef struct { const char* s; } ctx_t;
static uint64_t op_ours(void*c){ return (uint64_t)wia_strlen(((ctx_t*)c)->s); }
static uint64_t op_sys (void*c){ return (uint64_t)sys(((ctx_t*)c)->s); }
static const char* mk(size_t n){ char* s=malloc(n+1); for(size_t i=0;i<n;i++)s[i]=(char)('a'+(i&15)); s[n]=0; return s; }
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"strlen");
    static const size_t L[]={3,15,63,255,1023,8191,65535};
    static const char* N[]={"3","15","63","255","1023","8191","65535"};
    enum{K=7}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ cx[i].s=mk(L[i]); cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("strlen  (wia AVX2 vs ucrtbase)", cs, K, 200);
}
