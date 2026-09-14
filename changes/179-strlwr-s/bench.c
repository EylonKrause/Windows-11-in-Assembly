// changes/179-strlwr-s/bench.c
// Gate 2: time wia_strlwr_s against the live ucrtbase!_strlwr_s across size classes.
// In-place, so each iteration restores the buffer; both sides pay the same restore.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int wia_strlwr_s(char*, size_t);
typedef int (__cdecl *SLS)(char*, size_t);
static SLS sys;

typedef struct { const char* src; int n; size_t cch; } CASE;
static char work[2200];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  return (uint64_t)wia_strlwr_s(work,k->cch); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  return (uint64_t)sys(work,k->cch); }
#pragma optimize("", on)

static char s8[16], s32[64], s64[96], s254[300], s2048[2100], smixed[300];

static void fill(char* b,int n){ for(int i=0;i<n;i++) b[i]=(char)('A'+(i%26)); b[n]=0; }

int main(void){
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (SLS)GetProcAddress(hu,"_strlwr_s");
    fill(s8,8); fill(s32,32); fill(s64,64); fill(s254,254); fill(s2048,2048);
    /* a realistic mix: uppercase, already-lowercase, digits and high bytes that must not fold */
    for(int i=0;i<254;i++){
        int m=i%4;
        smixed[i] = (m==0)? (char)('A'+(i%26))
                  : (m==1)? (char)('a'+(i%26))
                  : (m==2)? (char)('0'+(i%10))
                  :         (char)(0x80 + (i%64));
    }
    smixed[254]=0;

    enum { N = 6 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"8","32","64","254","2048","254/mixed"};
    C[0].src=s8;     C[0].n=8;    C[0].cch=9;
    C[1].src=s32;    C[1].n=32;   C[1].cch=33;
    C[2].src=s64;    C[2].n=64;   C[2].cch=65;
    C[3].src=s254;   C[3].n=254;  C[3].cch=255;
    C[4].src=s2048;  C[4].n=2048; C[4].cch=2049;
    C[5].src=smixed; C[5].n=254;  C[5].cch=255;
    static const size_t bytes[] = { 8,32,64,254,2048,254 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase _strlwr_s (wia AVX2 validate-then-fold vs ucrtbase scalar)", cs, N, 300);
}
