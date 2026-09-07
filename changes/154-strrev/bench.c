// changes/154-strrev/bench.c
// Reversal is its own inverse, so repeated calls leave the buffer in the same two states and no
// per-iteration reset is needed.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern char* wia_strrev(char*);
typedef char* (__cdecl *fn)(char*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ return (uint64_t)(size_t)wia_strrev((char*)x); }
static uint64_t op_sys (void* x){ return (uint64_t)(size_t)sys((char*)x); }
#pragma optimize("", on)
static char b16[64], b31[64], b63[128], b254[320], b1024[1100], b4096[4200];
static void fill(char* p, int n){ for(int i=0;i<n;i++) p[i]=(char)('a'+(i%23)); p[n]=0; }
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"_strrev");
    fill(b16,16); fill(b31,31); fill(b63,63); fill(b254,254); fill(b1024,1024); fill(b4096,4096);
    static const char* N[]={"16 chars","31 chars","63 chars","254 chars","1024 chars","4096 chars"};
    static const int L[]={16,31,63,254,1024,4096};
    char* B[6]={b16,b31,b63,b254,b1024,b4096};
    static wia_case cs[6];
    for(int i=0;i<6;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=B[i]; }
    return wia_bench_compare("ucrtbase _strrev (wia AVX2 vs ucrtbase)", cs, 6, 300);
}
