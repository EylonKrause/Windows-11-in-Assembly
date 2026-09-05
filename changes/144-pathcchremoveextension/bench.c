// changes/144-pathcchremoveextension/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern long wia_pathcchremoveext(wchar_t*, unsigned long long);
typedef HRESULT (WINAPI *fn)(wchar_t*, size_t);
static fn sys;
typedef struct { const wchar_t* src; int n; unsigned long long cch; } CASE;
static wchar_t work[1200];
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2); return (uint64_t)wia_pathcchremoveext(work,k->cch); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2); return (uint64_t)sys(work,(size_t)k->cch); }
#pragma optimize("", on)
static wchar_t s16[32], s64[96], s254[300], s1024[1100], sreal[96];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=(i%20==19)?(wchar_t)0x5C:(L'a'+(i%23)); b[n-4]=L'.'; b[n]=0; }
int main(void){
    HMODULE kb=LoadLibraryW(L"kernelbase.dll"); sys=(fn)GetProcAddress(kb,"PathCchRemoveExtension");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024);
    { const wchar_t* r=L"C:\Program Files\Windows NT\Accessories\wordpad.exe";
      int i=0; for(; r[i]; ++i) sreal[i]=r[i]; sreal[i]=0; }
    static CASE C[5]; static wia_case cs[5];
    static const char* N[]={"16","64","254","1024","realpath"};
    static const int L[]={16,64,254,1024,49};
    C[0].src=s16;C[0].n=16;C[0].cch=17;    C[1].src=s64;C[1].n=64;C[1].cch=65;
    C[2].src=s254;C[2].n=254;C[2].cch=255; C[3].src=s1024;C[3].n=1024;C[3].cch=1025;
    C[4].src=sreal;C[4].n=49;C[4].cch=50;
    for(int i=0;i<5;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("kernelbase PathCchRemoveExtension (wia AVX2 vs kernelbase)", cs, 5, 300);
}
