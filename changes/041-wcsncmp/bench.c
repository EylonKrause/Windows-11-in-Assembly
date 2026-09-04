// changes/041-wcsncmp/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern int wia_wcsncmp(const wchar_t*, const wchar_t*, size_t);
typedef int (__cdecl *fn)(const wchar_t*, const wchar_t*, size_t);
static fn sys;
typedef struct { const wchar_t* a; const wchar_t* b; size_t n; } ctx_t;
// Built /Od (see build.bat): pure, loop-invariant args -> /O2 MSVC hoists the call out
// of the timing loop (bogus 0.00 ns). /Od => real symmetric call each iteration.
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)wia_wcsncmp(m->a,m->b,m->n); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)sys(m->a,m->b,m->n); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"wcsncmp");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        wchar_t* a=malloc((L[i]+1)*2); wchar_t* b=malloc((L[i]+1)*2);
        for(int k=0;k<L[i];k++) a[k]=b[k]=(wchar_t)(L'a'+(k%23));   // equal -> full n-char scan (worst case)
        a[L[i]]=b[L[i]]=0;
        cx[i].a=a; cx[i].b=b; cx[i].n=(size_t)L[i];
        cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("wcsncmp  (wia AVX2 vs ucrtbase, equal strings, full n scan)", cs, K, 200);
}
