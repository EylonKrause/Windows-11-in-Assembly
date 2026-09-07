// changes/161-strpbrk/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern const char* wia_strpbrk(const char*, const char*);
typedef char* (__cdecl *fn)(const char*, const char*);
static fn sys;
typedef struct { const char* s; const char* set; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; return (uint64_t)(size_t)wia_strpbrk(k->s,k->set); }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; return (uint64_t)(size_t)sys(k->s,k->set); }
#pragma optimize("", on)
static char s16[32], s64[96], s254[300], s1024[1100], searly[300];
static void fill(char* b,int n){ for(int i=0;i<n;i++) b[i]=(char)('a'+(i%3)); b[n]=0; }
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"strpbrk");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024);
    memset(searly,'a',254); searly[254]=0; searly[8]='Z';
    /* strings are drawn from {a,b,c}: a disjoint set means the scan runs to the terminator (NULL).
       The early-hit case is at index 8, not 0 -- see 158's RESULTS.md: a do-nothing indirect call
       costs 3.11 ns in this harness, so a hit at index 0 times the call rather than the code. */
    static CASE C[6]; static wia_case cs[6];
    static const char* N[]={"16 chars, 3-set","64 chars, 3-set","254 chars, 3-set","1024 chars, 3-set",
                            "254 chars, hit at 8","254 chars, 16-set"};
    static const int L[]={16,64,254,1024,254,254};
    C[0].s=s16; C[1].s=s64; C[2].s=s254; C[3].s=s1024; C[4].s=searly; C[5].s=s254;
    C[0].set=C[1].set=C[2].set=C[3].set="xyz";
    C[4].set="Z";
    C[5].set="defghijklmnopqrs";
    for(int i=0;i<6;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase strpbrk (wia AVX2 vs ucrtbase)", cs, 6, 300);
}
