// changes/148-wcstok-s/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern wchar_t* wia_wcstok_s(wchar_t*, const wchar_t*, wchar_t**);
typedef wchar_t* (__cdecl *fn)(wchar_t*, const wchar_t*, wchar_t**);
static fn sys;
typedef struct { const wchar_t* src; int n; const wchar_t* delim; } CASE;
static wchar_t work[1200];
/* wcstok_s mutates, so each iteration restores the input; both sides pay the same copy */
#pragma optimize("", off)
static uint64_t op_ours(void*x){ CASE*k=(CASE*)x; memcpy(work,k->src,(size_t)(k->n+1)*2); wchar_t* c=0;
    return (uint64_t)(size_t)wia_wcstok_s(work,k->delim,&c); }
static uint64_t op_sys (void*x){ CASE*k=(CASE*)x; memcpy(work,k->src,(size_t)(k->n+1)*2); wchar_t* c=0;
    return (uint64_t)(size_t)sys(work,k->delim,&c); }
#pragma optimize("", on)
static wchar_t s16[32], s64[96], s254[300], s1024[1100], scsv[300];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23)); b[n]=0; }
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"wcstok_s");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024); fill(scsv,254);
    for(int i=8;i<254;i+=16) scsv[i]=L',';
    static CASE C[5]; static wia_case cs[5];
    static const char* N[]={"16ch/1tok","64ch/1tok","254ch/1tok","1024ch/1tok","254ch/csv"};
    static const int L[]={16,64,254,1024,254};
    C[0].src=s16;C[0].n=16;     C[1].src=s64;C[1].n=64;    C[2].src=s254;C[2].n=254;
    C[3].src=s1024;C[3].n=1024; C[4].src=scsv;C[4].n=254;
    for(int i=0;i<5;++i){ C[i].delim=L","; cs[i].label=N[i]; cs[i].bytes=L[i]*2;
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase wcstok_s (wia AVX2 vs ucrtbase)", cs, 5, 300);
}
