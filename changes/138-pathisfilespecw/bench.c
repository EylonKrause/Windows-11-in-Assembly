// changes/138-pathisfilespecw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern int wia_pathisfilespecw(const wchar_t*);
typedef int (WINAPI *fn)(const wchar_t*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ return (uint64_t)wia_pathisfilespecw((const wchar_t*)c); }
static uint64_t op_sys (void*c){ return (uint64_t)sys((const wchar_t*)c); }
#pragma optimize("", on)
static wchar_t s16[32], s64[96], s254[300], s1024[1100], sbad[300];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=L'a'+(i%23); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"PathIsFileSpecW");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024); fill(sbad,254);
    sbad[120]=(wchar_t)0x5C;                      /* a backslash halfway in */
    static const wchar_t* P[5]; static const char* N[]={"16","64","254","1024","254/bad@120"};
    static const int L[]={16,64,254,1024,254};
    P[0]=s16; P[1]=s64; P[2]=s254; P[3]=s1024; P[4]=sbad;
    enum{K=5}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=(void*)P[i]; }
    return wia_bench_compare("shlwapi PathIsFileSpecW (wia AVX2 vs shlwapi)", cs, K, 300);
}
