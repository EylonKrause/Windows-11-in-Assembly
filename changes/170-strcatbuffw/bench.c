// changes/170-strcatbuffw/bench.c
// Gate 2: time wia_strcatbuffw against the live shlwapi!StrCatBuffW across size classes.
// In-place, so each iteration restores the destination; both sides pay the same restore.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "bench.h"

extern wchar_t* wia_strcatbuffw(wchar_t*, const wchar_t*, int);
typedef PWSTR (WINAPI *SCB)(PWSTR, PCWSTR, int);
static SCB sys;

typedef struct { const wchar_t* dst0; int dl; const wchar_t* src; int cch; } CASE;
static wchar_t work[2600];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->dst0,(size_t)(k->dl+1)*2);
                                  return (uint64_t)(UINT_PTR)wia_strcatbuffw(work,k->src,k->cch); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->dst0,(size_t)(k->dl+1)*2);
                                  return (uint64_t)(UINT_PTR)sys(work,k->src,k->cch); }
#pragma optimize("", on)

static wchar_t d0[8], d64[96], d254[300];
static wchar_t s16[32], s64[96], s254[300], s1024[1100];
static void fill(wchar_t* b,int n,wchar_t base){ for(int i=0;i<n;i++) b[i]=(wchar_t)(base+(i%23)); b[n]=0; }

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (SCB)GetProcAddress(h,"StrCatBuffW");
    fill(d0,0,L'a'); fill(d64,64,L'a'); fill(d254,254,L'a');
    fill(s16,16,L'A'); fill(s64,64,L'A'); fill(s254,254,L'A'); fill(s1024,1024,L'A');

    enum { N = 7 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"empty+16","empty+254","64+16","64+254","254+254","254+1024","64+254/bound80"};
    C[0].dst0=d0;   C[0].dl=0;   C[0].src=s16;   C[0].cch=64;
    C[1].dst0=d0;   C[1].dl=0;   C[1].src=s254;  C[1].cch=300;
    C[2].dst0=d64;  C[2].dl=64;  C[2].src=s16;   C[2].cch=128;
    C[3].dst0=d64;  C[3].dl=64;  C[3].src=s254;  C[3].cch=400;
    C[4].dst0=d254; C[4].dl=254; C[4].src=s254;  C[4].cch=600;
    C[5].dst0=d254; C[5].dl=254; C[5].src=s1024; C[5].cch=1400;
    C[6].dst0=d64;  C[6].dl=64;  C[6].src=s254;  C[6].cch=80;   /* bound truncates the append */
    static const size_t bytes[] = { 32,508,32,508,508,2048,32 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrCatBuffW (wia AVX2 wcslen + fused copy vs shlwapi scalar)", cs, N, 300);
}
