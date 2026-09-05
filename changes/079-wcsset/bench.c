// changes/079-wcsset/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include "bench.h"
extern wchar_t* wia_wcsset(wchar_t*, wchar_t);
typedef wchar_t* (__cdecl *fn)(wchar_t*, wchar_t);
static fn sys;
typedef struct { wchar_t* s; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ return (uint64_t)(uintptr_t)wia_wcsset(((ctx_t*)c)->s,L'X'); }
static uint64_t op_sys (void*c){ return (uint64_t)(uintptr_t)sys(((ctx_t*)c)->s,L'X'); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"_wcsset");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ wchar_t* s=malloc((L[i]+1)*2); for(int k=0;k<L[i];k++)s[k]=(wchar_t)('a'+(k%23)); s[L[i]]=0;
        cx[i].s=s; cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("_wcsset  (wia AVX2 broadcast-fill vs ucrtbase)", cs, K, 200);
}
