// changes/155-wcsrev/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include "bench.h"
extern wchar_t* wia_wcsrev(wchar_t*);
typedef wchar_t* (__cdecl *fn)(wchar_t*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ return (uint64_t)(size_t)wia_wcsrev((wchar_t*)x); }
static uint64_t op_sys (void* x){ return (uint64_t)(size_t)sys((wchar_t*)x); }
#pragma optimize("", on)
static wchar_t b16[64], b31[64], b63[128], b254[320], b1024[1100], b4096[4200];
static void fill(wchar_t* p, int n){ for(int i=0;i<n;i++) p[i]=(wchar_t)(L'a'+(i%23)); p[n]=0; }
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"_wcsrev");
    fill(b16,16); fill(b31,31); fill(b63,63); fill(b254,254); fill(b1024,1024); fill(b4096,4096);
    static const char* N[]={"16 wchars","31 wchars","63 wchars","254 wchars","1024 wchars","4096 wchars"};
    static const int L[]={16,31,63,254,1024,4096};
    wchar_t* B[6]={b16,b31,b63,b254,b1024,b4096};
    static wia_case cs[6];
    for(int i=0;i<6;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=B[i]; }
    return wia_bench_compare("ucrtbase _wcsrev (wia AVX2 vs ucrtbase)", cs, 6, 300);
}
