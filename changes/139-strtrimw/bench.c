// changes/139-strtrimw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern int wia_strtrimw(wchar_t*, const wchar_t*);
typedef int (WINAPI *fn)(wchar_t*, const wchar_t*);
static fn sys;
typedef struct { const wchar_t* src; int n; const wchar_t* set; } CASE;
static wchar_t work[1200];
/* StrTrimW mutates, so each iteration restores the input first; both sides pay the same copy. */
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2); return (uint64_t)wia_strtrimw(work,k->set); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2); return (uint64_t)sys(work,k->set); }
#pragma optimize("", on)
static wchar_t s64[96], s254[300], s1024[1100], slead[300];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=L'a'+(i%23); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrTrimW");
    fill(s64,64); fill(s254,254); fill(s1024,1024); fill(slead,254);
    for(int i=0;i<8;i++) slead[i]=L' ';
    for(int i=0;i<4;i++) slead[253-i]=L' ';
    static CASE C[4]; static wia_case cs[4];
    static const char* N[]={"64/notrim","254/notrim","1024/notrim","254/trim8+4"};
    static const int L[]={64,254,1024,254};
    C[0].src=s64;   C[0].n=64;   C[0].set=L" \t";
    C[1].src=s254;  C[1].n=254;  C[1].set=L" \t";
    C[2].src=s1024; C[2].n=1024; C[2].set=L" \t";
    C[3].src=slead; C[3].n=254;  C[3].set=L" \t";
    for(int i=0;i<4;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrTrimW (wia AVX2 vs shlwapi)", cs, 4, 200);
}
