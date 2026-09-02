// changes/003-wcschr/bench.c -- wia_wcschr vs live ucrtbase wcschr, target absent (full scan to terminator).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include "bench.h"
extern wchar_t* wia_wcschr(const wchar_t*, wchar_t);
typedef wchar_t* (__cdecl *fn)(const wchar_t*, wchar_t);
static fn sys;
typedef struct { const wchar_t* s; } ctx_t;
static uint64_t op_ours(void*c){ return (uint64_t)(uintptr_t)wia_wcschr(((ctx_t*)c)->s, L'@'); }
static uint64_t op_sys (void*c){ return (uint64_t)(uintptr_t)sys(((ctx_t*)c)->s, L'@'); }
static const wchar_t* mk(size_t len){ wchar_t* s=(wchar_t*)malloc((len+1)*2); for(size_t i=0;i<len;++i) s[i]=L'a'+(wchar_t)(i&15); s[len]=0; return s; }
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"wcschr");
    static const size_t L[]={3,15,63,255,1023,8191,65535};
    static const char* N[]={"3","15","63","255","1023","8191","65535"};
    enum{K=7}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ cx[i].s=mk(L[i]); cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("wcschr  (wia AVX2 vs ucrtbase, target absent)", cs, K, 200);
}
