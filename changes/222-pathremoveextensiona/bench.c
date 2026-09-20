// changes/222-pathremoveextensiona/bench.c
// Gate 2: time wia_pathremoveexta against the live shlwapi!PathRemoveExtensionA.
//
// The same classes change 140 used for the wide form, at the same CHARACTER counts, so the two are
// directly comparable. It mutates, so each iteration restores the input and both sides pay the same
// copy.
//
// NOTE the 1024 class: at 260 characters or more the export hits its MAX_PATH guard and returns
// without touching the buffer, so that class times the LENGTH SCAN alone. That is a real caller
// path, not a degenerate one; a long path is exactly when the guard fires.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern void wia_pathremoveexta(char*);
typedef void (WINAPI *fn)(char*);
static fn sys;
typedef struct { const char* src; int n; } CASE;
static char work[1200];
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)); wia_pathremoveexta(work); return (uint64_t)work[0]; }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)); sys(work); return (uint64_t)work[0]; }
#pragma optimize("", on)
static char s16[32], s64[96], s254[300], s1024[1100], sreal[96];
static void fill(char* b,int n){ for(int i=0;i<n;i++) b[i]=(char)((i%20==19)?0x5C:('a'+(i%23))); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"PathRemoveExtensionA");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024);
    strcpy(sreal, "C:\Program Files\Windows NT\Accessories\wordpad.exe");
    static CASE C[5]; static wia_case cs[5];
    static const char* N[]={"16","64","254","1024 (past the guard)","realpath"};
    static const int L[]={16,64,254,1024,49};
    C[0].src=s16;C[0].n=16; C[1].src=s64;C[1].n=64; C[2].src=s254;C[2].n=254;
    C[3].src=s1024;C[3].n=1024; C[4].src=sreal;C[4].n=(int)strlen(sreal);
    for(int i=0;i<5;++i){ cs[i].label=N[i]; cs[i].bytes=(size_t)L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathRemoveExtensionA (wia AVX2 vs an MBCS per-character walk)", cs, 5, 300);
}
