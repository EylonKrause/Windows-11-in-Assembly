// changes/150-strcpy-s/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern int wia_strcpy_s(char*, size_t, const char*);
typedef int (__cdecl *fn)(char*, size_t, const char*);
static fn sys;
typedef struct { char* d; size_t size; const char* s; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; return (uint64_t)(unsigned)wia_strcpy_s(k->d,k->size,k->s); }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; return (uint64_t)(unsigned)sys(k->d,k->size,k->s); }
#pragma optimize("", on)
static char dst[8192], src[8192];
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"strcpy_s");
    memset(src,'a',sizeof src);
    static const int L[]={7,15,31,63,254,1024,4096};
    static const char* N[]={"7 chars","15 chars","31 chars","63 chars","254 chars","1024 chars","4096 chars"};
    static CASE C[7]; static wia_case cs[7];
    for(int i=0;i<7;++i){
        C[i].d=dst; C[i].size=sizeof dst; C[i].s=src+ (sizeof src - 1 - L[i]);  /* one string per length */
        cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    src[sizeof src - 1]=0;
    return wia_bench_compare("ucrtbase strcpy_s (wia AVX2 vs ucrtbase)", cs, 7, 300);
}
