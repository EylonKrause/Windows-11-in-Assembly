// changes/180-wcslwr-s/bench.c
// Gate 2: time wia_wcslwr_s against the live ucrtbase!_wcslwr_s across size classes.
// In-place, so each iteration restores the buffer; both sides pay the same restore.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "bench.h"

extern int wia_wcslwr_s(wchar_t*, size_t);
typedef int (__cdecl *WUS)(wchar_t*, size_t);
static WUS sys;

typedef struct { const wchar_t* src; int n; size_t cch; } CASE;
static wchar_t work[2200];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2);
                                  return (uint64_t)wia_wcslwr_s(work,k->cch); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2);
                                  return (uint64_t)sys(work,k->cch); }
#pragma optimize("", on)

static wchar_t s8[16], s16[32], s64[96], s254[300], s1024[1100], smixed[300];

static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=(wchar_t)(L'A'+(i%26)); b[n]=0; }

int main(void){
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (WUS)GetProcAddress(hu,"_wcslwr_s");
    fill(s8,8); fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024);
    /* a realistic mix: letters, digits and already-uppercase, so the fold mask varies */
    for(int i=0;i<254;i++){
        int m=i%4;
        smixed[i] = (m==0)? (wchar_t)(L'A'+(i%26))
                  : (m==1)? (wchar_t)(L'A'+(i%26))
                  : (m==2)? (wchar_t)(L'0'+(i%10))
                  :         (wchar_t)(0x00E0+(i%16));
    }
    smixed[254]=0;

    enum { N = 6 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"8","16","64","254","1024","254/mixed"};
    C[0].src=s8;     C[0].n=8;    C[0].cch=9;
    C[1].src=s16;    C[1].n=16;   C[1].cch=17;
    C[2].src=s64;    C[2].n=64;   C[2].cch=65;
    C[3].src=s254;   C[3].n=254;  C[3].cch=255;
    C[4].src=s1024;  C[4].n=1024; C[4].cch=1025;
    C[5].src=smixed; C[5].n=254;  C[5].cch=255;
    static const size_t bytes[] = { 16,32,128,508,2048,508 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase _wcslwr_s (wia AVX2 validate-then-fold vs ucrtbase scalar)", cs, N, 300);
}
