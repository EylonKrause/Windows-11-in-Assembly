// changes/153-wcscat-s/bench.c
// Each op re-terminates dst at the prefix length first, so every iteration appends to the same
// starting string. That one store is paid identically by both sides.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include "bench.h"
extern int wia_wcscat_s(wchar_t*, size_t, const wchar_t*);
typedef int (__cdecl *fn)(wchar_t*, size_t, const wchar_t*);
static fn sys;
typedef struct { wchar_t* d; size_t size; const wchar_t* s; int pre; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; k->d[k->pre]=0; return (uint64_t)(unsigned)wia_wcscat_s(k->d,k->size,k->s); }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; k->d[k->pre]=0; return (uint64_t)(unsigned)sys(k->d,k->size,k->s); }
#pragma optimize("", on)
static wchar_t dst[16384], src[8192];
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"wcscat_s");
    for(int i=0;i<8192;i++) src[i]=L'a';
    src[8191]=0;
    for(int i=0;i<16384;i++) dst[i]=L'D';
    static const int L[]  ={7,15,31,63,254,1024,4096,16};
    static const int PRE[]={0,0, 0, 0, 0,  0,   0,   1000};
    static const char* N[]={"append 7","append 15","append 31","append 63","append 254",
                            "append 1024","append 4096","append 16 to a 1000-wchar dst"};
    static CASE C[8]; static wia_case cs[8];
    for(int i=0;i<8;++i){
        C[i].d=dst; C[i].size=16384; C[i].s=src + (8191 - L[i]); C[i].pre=PRE[i];
        cs[i].label=N[i]; cs[i].bytes=(L[i]+PRE[i])*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    return wia_bench_compare("ucrtbase wcscat_s (wia AVX2 vs ucrtbase)", cs, 8, 300);
}
