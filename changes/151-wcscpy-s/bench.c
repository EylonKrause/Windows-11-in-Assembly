// changes/151-wcscpy-s/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include "bench.h"
extern int wia_wcscpy_s(wchar_t*, size_t, const wchar_t*);
typedef int (__cdecl *fn)(wchar_t*, size_t, const wchar_t*);
static fn sys;
typedef struct { wchar_t* d; size_t size; const wchar_t* s; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; return (uint64_t)(unsigned)wia_wcscpy_s(k->d,k->size,k->s); }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; return (uint64_t)(unsigned)sys(k->d,k->size,k->s); }
#pragma optimize("", on)
static wchar_t dst[8192], src[8192];
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"wcscpy_s");
    for(int i=0;i<8192;i++) src[i]=L'a';
    src[8191]=0;
    static const int L[]={7,15,31,63,254,1024,4096};
    static const char* N[]={"7 wchars","15 wchars","31 wchars","63 wchars","254 wchars","1024 wchars","4096 wchars"};
    static CASE C[7]; static wia_case cs[7];
    for(int i=0;i<7;++i){
        C[i].d=dst; C[i].size=8192; C[i].s=src + (8191 - L[i]);
        cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    return wia_bench_compare("ucrtbase wcscpy_s (wia AVX2 vs ucrtbase)", cs, 7, 300);
}
