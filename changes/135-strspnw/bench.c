// changes/135-strspnw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern int wia_strspnw(const wchar_t*, const wchar_t*);
typedef int (WINAPI *fn)(const wchar_t*, const wchar_t*);
static fn sys;
typedef struct { const wchar_t* s; const wchar_t* set; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; return (uint64_t)wia_strspnw(k->s,k->set); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; return (uint64_t)sys(k->s,k->set); }
#pragma optimize("", on)
static wchar_t s16[32], s64[96], s254[300], s1024[1100];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=L'a'+(i%23); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrSpnW");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024);
    static const wchar_t* BIG=L"abcdefghijklmnopqrstuvw";   /* 23 chars */
    static const wchar_t* SML=L"abc";
    static CASE C[5]; static wia_case cs[5];
    static const char* N[]={"16/set23","64/set23","254/set23","1024/set23","254/set3-stop0"};
    static const int L[]={16,64,254,1024,254};
    C[0].s=s16;   C[0].set=BIG;
    C[1].s=s64;   C[1].set=BIG;
    C[2].s=s254;  C[2].set=BIG;
    C[3].s=s1024; C[3].set=BIG;
    C[4].s=s254;  C[4].set=SML;      /* stops at index 3 -- the short-span case */
    for(int i=0;i<5;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrSpnW (wia AVX2 vs shlwapi)", cs, 5, 200);
}
