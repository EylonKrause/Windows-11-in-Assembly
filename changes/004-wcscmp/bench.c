// changes/004-wcscmp/bench.c: equal strings (worst case: full scan) vs live ucrtbase wcscmp.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include "bench.h"
extern int wia_wcscmp(const wchar_t*, const wchar_t*);
typedef int (__cdecl *fn)(const wchar_t*, const wchar_t*);
static fn sys;
typedef struct { const wchar_t* a; const wchar_t* b; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)wia_wcscmp(m->a,m->b); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)sys(m->a,m->b); }
static const wchar_t* mk(size_t len){ wchar_t* s=(wchar_t*)malloc((len+1)*2); for(size_t i=0;i<len;++i)s[i]=L'a'+(wchar_t)(i&15); s[len]=0; return s; }
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"wcscmp");
    static const size_t L[]={3,15,63,255,1023,8191};
    static const char* N[]={"3","15","63","255","1023","8191"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ cx[i].a=mk(L[i]); cx[i].b=mk(L[i]); cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("wcscmp  (wia AVX2 vs ucrtbase, equal strings)", cs, K, 200);
}
