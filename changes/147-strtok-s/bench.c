// changes/147-strtok-s/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern char* wia_strtok_s(char*, const char*, char**);
typedef char* (__cdecl *fn)(char*, const char*, char**);
static fn sys;
typedef struct { const char* src; int n; const char* delim; } CASE;
static char work[1200];
/* strtok_s mutates, so each iteration restores the input; both sides pay the same copy */
#pragma optimize("", off)
static uint64_t op_ours(void*x){ CASE*k=(CASE*)x; memcpy(work,k->src,(size_t)k->n+1); char* c=0;
    return (uint64_t)(size_t)wia_strtok_s(work,k->delim,&c); }
static uint64_t op_sys (void*x){ CASE*k=(CASE*)x; memcpy(work,k->src,(size_t)k->n+1); char* c=0;
    return (uint64_t)(size_t)sys(work,k->delim,&c); }
#pragma optimize("", on)
static char s16[32], s64[96], s254[300], s1024[1100], scsv[300];
static void fill(char* b,int n){ for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23)); b[n]=0; }
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"strtok_s");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024); fill(scsv,254);
    for(int i=8;i<254;i+=16) scsv[i]=',';
    static CASE C[5]; static wia_case cs[5];
    static const char* N[]={"16B/1tok","64B/1tok","254B/1tok","1KB/1tok","254B/csv"};
    static const int L[]={16,64,254,1024,254};
    C[0].src=s16;C[0].n=16;   C[1].src=s64;C[1].n=64;   C[2].src=s254;C[2].n=254;
    C[3].src=s1024;C[3].n=1024; C[4].src=scsv;C[4].n=254;
    for(int i=0;i<5;++i){ C[i].delim=","; cs[i].label=N[i]; cs[i].bytes=L[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase strtok_s (wia AVX2 vs ucrtbase)", cs, 5, 300);
}
