// changes/145-swab/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern void wia_swab(char*, char*, int);
typedef void (__cdecl *fn)(char*, char*, int);
static fn sys;
static char SRC[8200], DST[8200];
typedef struct { int n; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ wia_swab(SRC,DST,((CASE*)c)->n); return (uint64_t)(unsigned char)DST[0]; }
static uint64_t op_sys (void*c){ sys(SRC,DST,((CASE*)c)->n);      return (uint64_t)(unsigned char)DST[0]; }
#pragma optimize("", on)
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"_swab");
    for(int i=0;i<8200;i++) SRC[i]=(char)(i*7+1);
    static const int N[]={16,64,508,2048,8192};
    static const char* L[]={"16B","64B","508B","2KB","8KB"};
    enum{K=5}; static CASE C[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ C[i].n=N[i];
        cs[i].label=L[i]; cs[i].bytes=N[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase _swab (wia vpshufb vs ucrtbase)", cs, K, 300);
}
