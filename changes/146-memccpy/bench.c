// changes/146-memccpy/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern void* wia_memccpy(void*, const void*, int, unsigned long long);
typedef void* (__cdecl *fn)(void*, const void*, int, size_t);
static fn sys;
static char SRC[8300], DST[8300];
typedef struct { int n; int c; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*x){ CASE*k=(CASE*)x; return (uint64_t)(size_t)wia_memccpy(DST,SRC,k->c,(unsigned long long)k->n); }
static uint64_t op_sys (void*x){ CASE*k=(CASE*)x; return (uint64_t)(size_t)sys(DST,SRC,k->c,(size_t)k->n); }
#pragma optimize("", on)
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"_memccpy");
    for(int i=0;i<8300;i++) SRC[i]=(char)((i*7+1)|1);
    static const int N[]={16,64,508,2048,8192,508};
    static const int C[]={0x5A,0x5A,0x5A,0x5A,0x5A,0};      /* last: delimiter present at 200 */
    static const char* L[]={"16B/absent","64B/absent","508B/absent","2KB/absent","8KB/absent","508B/hit@200"};
    SRC[200]=0x21;
    enum{K=6}; static CASE Cs[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ Cs[i].n=N[i]; Cs[i].c=(i==5)?0x21:C[i];
        cs[i].label=L[i]; cs[i].bytes=(i==5)?201:N[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&Cs[i]; }
    return wia_bench_compare("ucrtbase _memccpy (wia AVX2 vs ucrtbase)", cs, K, 300);
}
