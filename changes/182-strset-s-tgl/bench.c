// changes/182-strset-s/bench.c
// Gate 2: time wia_strset_s against the live ucrtbase!_strset_s across size classes.
// In-place, so each iteration restores the buffer; both sides pay the same restore.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int wia_strset_s(char*, size_t, int);
typedef int (__cdecl *SSS)(char*, size_t, int);
static SSS sys;

// The "254/partial" class deliberately takes the EINVAL path, which calls ucrtbase's
// _invalid_parameter_noinfo. With no handler installed that default-terminates the process via
// __fastfail (0xC0000409), so a silent handler is installed in ucrtbase itself -- the same module
// both our error path and the live export route through. Both sides pay the identical call.
static void __cdecl silent(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                           unsigned d, uintptr_t e){ (void)a;(void)b;(void)c;(void)d;(void)e; }

typedef struct { const char* src; int n; size_t cch; } CASE;
static char work[2200];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  return (uint64_t)wia_strset_s(work,k->cch,'x'); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  return (uint64_t)sys(work,k->cch,'x'); }
#pragma optimize("", on)

static char s8[16], s32[64], s64[96], s254[300], s2048[2100];
static void fill(char* b,int n){ for(int i=0;i<n;i++) b[i]=(char)('a'+(i%26)); b[n]=0; }

int main(void){
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (SSS)GetProcAddress(hu,"_strset_s");
    { typedef void* (__cdecl *SIPH)(void*);
      SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
      if(set) set((void*)silent); }
    fill(s8,8); fill(s32,32); fill(s64,64); fill(s254,254); fill(s2048,2048);

    enum { N = 6 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"8","32","64","254","2048","254/partial"};
    C[0].src=s8;    C[0].n=8;    C[0].cch=9;
    C[1].src=s32;   C[1].n=32;   C[1].cch=33;
    C[2].src=s64;   C[2].n=64;   C[2].cch=65;
    C[3].src=s254;  C[3].n=254;  C[3].cch=255;
    C[4].src=s2048; C[4].n=2048; C[4].cch=2049;
    C[5].src=s254;  C[5].n=254;  C[5].cch=128;   /* too small -> partial fill then empty */
    static const size_t bytes[] = { 8,32,64,254,2048,127 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase _strset_s (wia AVX2 scan + broadcast fill vs ucrtbase scalar)", cs, N, 300);
}
