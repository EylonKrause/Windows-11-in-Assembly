// changes/221-pathremoveblanksa/bench.c
// Gate 2: time wia_pathremoveblanksa against the live shlwapi!PathRemoveBlanksA.
//
// The same classes change 141 used for the wide form, at the same CHARACTER counts, so the two are
// directly comparable. It mutates, so each iteration restores the input and both sides pay the same
// copy.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern void wia_pathremoveblanksa(char*);
typedef void (WINAPI *fn)(char*);
static fn sys;
typedef struct { const char* src; int n; } CASE;
static char work[1200];
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)); wia_pathremoveblanksa(work); return (uint64_t)work[0]; }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)); sys(work); return (uint64_t)work[0]; }
#pragma optimize("", on)
static char s16[32], s64[96], s254[300], s1024[1100], sreal[96];
static void fill(char* b,int n){ for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23)); if(n>2){b[0]=' ';b[1]=' ';b[n-1]=' ';} b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"PathRemoveBlanksA");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024);
    strcpy(sreal, "C:\Program Files\Windows NT\Accessories\wordpad.exe");
    static CASE C[5]; static wia_case cs[5];
    static const char* N[]={"16","64","254","1024","realpath"};
    static const int L[]={16,64,254,1024,49};
    C[0].src=s16;C[0].n=16; C[1].src=s64;C[1].n=64; C[2].src=s254;C[2].n=254;
    C[3].src=s1024;C[3].n=1024; C[4].src=sreal;C[4].n=(int)strlen(sreal);
    for(int i=0;i<5;++i){ cs[i].label=N[i]; cs[i].bytes=(size_t)L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathRemoveBlanksA (wia AVX2 vs an MBCS per-character walk)", cs, 5, 300);
}
