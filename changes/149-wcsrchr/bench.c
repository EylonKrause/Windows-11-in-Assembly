// changes/149-wcsrchr/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern const wchar_t* wia_wcsrchr(const wchar_t*, wchar_t);
typedef wchar_t* (__cdecl *fn)(const wchar_t*, wchar_t);
static fn sys;
typedef struct { const wchar_t* s; wchar_t c; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*x){ CASE*k=(CASE*)x; return (uint64_t)(size_t)wia_wcsrchr(k->s,k->c); }
static uint64_t op_sys (void*x){ CASE*k=(CASE*)x; return (uint64_t)(size_t)sys(k->s,k->c); }
#pragma optimize("", on)
static wchar_t s16[32], s64[96], s254[300], s1024[1100], s2048[2100], spath[300];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23)); b[n]=0; }
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"wcsrchr");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024); fill(s2048,2048); fill(spath,254);
    for(int i=19;i<254;i+=20) spath[i]=(wchar_t)0x5C;      /* path-like: last backslash near the end */
    static CASE C[6]; static wia_case cs[6];
    static const char* N[]={"16/miss","64/miss","254/miss","1024/miss","2048/miss","254/path-lastsep"};
    static const int L[]={16,64,254,1024,2048,254};
    C[0].s=s16;   C[1].s=s64;   C[2].s=s254; C[3].s=s1024; C[4].s=s2048; C[5].s=spath;
    for(int i=0;i<5;++i) C[i].c=L'#';
    C[5].c=(wchar_t)0x5C;
    for(int i=0;i<6;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase wcsrchr (wia AVX2 vs ucrtbase)", cs, 6, 300);
}
