// changes/131-strchrw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern const wchar_t* wia_strchrw(const wchar_t*, wchar_t);
typedef wchar_t* (WINAPI *fn)(const wchar_t*, wchar_t);
static fn sys;
typedef struct { const wchar_t* s; wchar_t c; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)wia_strchrw(k->s,k->c); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)sys(k->s,k->c); }
#pragma optimize("", on)
static wchar_t b8[16], b32[64], b128[256], b254[300], b1024[1100];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=L'a'+(i%23); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrChrW");
    fill(b8,8); fill(b32,32); fill(b128,128); fill(b254,254); fill(b1024,1024);
    static CASE C[5]; static wia_case cs[5];
    static const wchar_t* P[5]; static const char* N[]={"8","32","128","254","1024"};
    static const int L[]={8,32,128,254,1024};
    P[0]=b8; P[1]=b32; P[2]=b128; P[3]=b254; P[4]=b1024;
    for(int i=0;i<5;++i){ C[i].s=P[i]; C[i].c=L'z';           // miss -> full scan
        cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrChrW (wia AVX2 vs shlwapi)", cs, 5, 300);
}
