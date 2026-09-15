// changes/218-strtrima/bench.c
// Gate 2: time wia_strtrima against the live shlwapi!StrTrimA.
//
// The same classes change 139 used for the wide form, at the same CHARACTER counts, so the two are
// directly comparable. StrTrimA mutates, so each iteration restores the input first and both sides
// pay the same copy.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern int wia_strtrima(char*, const char*);
typedef BOOL (WINAPI *fn)(char*, const char*);
static fn sys;
typedef struct { const char* src; int n; const char* set; } CASE;
static char work[2048];
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)); return (uint64_t)wia_strtrima(work,k->set); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)); return (uint64_t)sys(work,k->set); }
#pragma optimize("", on)
static char s64[96], s254[300], s1024[1100], slead[300];
static void fill(char* b,int n){ for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23)); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrTrimA");
    fill(s64,64); fill(s254,254); fill(s1024,1024); fill(slead,254);
    for(int i=0;i<8;i++) slead[i]=' ';
    for(int i=0;i<4;i++) slead[253-i]=' ';
    static CASE C[4]; static wia_case cs[4];
    static const char* N[]={"64/notrim","254/notrim","1024/notrim","254/trim8+4"};
    static const int L[]={64,254,1024,254};
    C[0].src=s64;   C[0].n=64;   C[0].set=" \t";
    C[1].src=s254;  C[1].n=254;  C[1].set=" \t";
    C[2].src=s1024; C[2].n=1024; C[2].set=" \t";
    C[3].src=slead; C[3].n=254;  C[3].set=" \t";
    for(int i=0;i<4;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrTrimA (wia 256-bit set + AVX2 vs an MBCS per-character walk)", cs, 4, 200);
}
