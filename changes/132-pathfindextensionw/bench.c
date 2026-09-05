// changes/132-pathfindextensionw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern const wchar_t* wia_pathfindextw(const wchar_t*);
typedef wchar_t* (WINAPI *fn)(const wchar_t*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ return (uint64_t)(size_t)wia_pathfindextw((const wchar_t*)c); }
static uint64_t op_sys (void*c){ return (uint64_t)(size_t)sys((const wchar_t*)c); }
#pragma optimize("", on)
static wchar_t s16[32], s64[96], s128[160], s254[300], real[64];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=(i%20==19)?L'\\':(L'a'+(i%23)); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"PathFindExtensionW");
    fill(s16,16); fill(s64,64); fill(s128,128); fill(s254,254);
    { const wchar_t* r=L"C:\\Program Files\\Windows NT\\Accessories\\wordpad.exe";
      int i=0; for(; r[i]; ++i) real[i]=r[i]; real[i]=0; }
    static const wchar_t* P[5]; static const char* N[]={"16","64","128","254","realpath"};
    static const int L[]={16,64,128,254,49};
    P[0]=s16; P[1]=s64; P[2]=s128; P[3]=s254; P[4]=real;
    enum{K=5}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=(void*)P[i]; }
    return wia_bench_compare("shlwapi PathFindExtensionW (wia AVX2 vs shlwapi)", cs, K, 300);
}
