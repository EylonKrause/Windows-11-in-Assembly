// changes/152-strcat-s/bench.c
// Each op re-terminates dst at the prefix length first, so every iteration appends to the same
// starting string. That one store is paid identically by both sides.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern int wia_strcat_s(char*, size_t, const char*);
typedef int (__cdecl *fn)(char*, size_t, const char*);
static fn sys;
typedef struct { char* d; size_t size; const char* s; int pre; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; k->d[k->pre]=0; return (uint64_t)(unsigned)wia_strcat_s(k->d,k->size,k->s); }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; k->d[k->pre]=0; return (uint64_t)(unsigned)sys(k->d,k->size,k->s); }
#pragma optimize("", on)
static char dst[16384], src[8192];
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"strcat_s");
    memset(src,'a',sizeof src); src[sizeof src - 1]=0;
    memset(dst,'D',sizeof dst);
    static const int L[]  ={7,15,31,63,254,1024,4096,16};
    static const int PRE[]={0,0, 0, 0, 0,  0,   0,   1000};
    static const char* N[]={"append 7","append 15","append 31","append 63","append 254",
                            "append 1024","append 4096","append 16 to a 1000-char dst"};
    static CASE C[8]; static wia_case cs[8];
    for(int i=0;i<8;++i){
        C[i].d=dst; C[i].size=sizeof dst; C[i].s=src + (sizeof src - 1 - L[i]); C[i].pre=PRE[i];
        cs[i].label=N[i]; cs[i].bytes=L[i]+PRE[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    return wia_bench_compare("ucrtbase strcat_s (wia AVX2 vs ucrtbase)", cs, 8, 300);
}
