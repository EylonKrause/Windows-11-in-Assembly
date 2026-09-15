// changes/220-strchra/bench.c
// Gate 2: time wia_strchra against the live shlwapi!StrChrA.
//
// The same classes change 131 used for the wide form, at the same CHARACTER counts, so the two are
// directly comparable. The target is absent from every class, which forces the full scan -- the
// worst case and the one a caller hits when testing "does this path contain a colon".
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern const char* wia_strchra(const char*, WORD);
typedef char* (WINAPI *fn)(const char*, WORD);
static fn sys;
typedef struct { const char* s; WORD c; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)wia_strchra(k->s,k->c); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)sys(k->s,k->c); }
#pragma optimize("", on)
static char b8[16], b32[64], b128[256], b254[300], b1024[1100];
static void fill(char* b,int n){ for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23)); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrChrA");
    fill(b8,8); fill(b32,32); fill(b128,128); fill(b254,254); fill(b1024,1024);
    static CASE C[5]; static wia_case cs[5];
    static const char* P[5]; static const char* N[]={"8","32","128","254","1024"};
    static const int L[]={8,32,128,254,1024};
    P[0]=b8; P[1]=b32; P[2]=b128; P[3]=b254; P[4]=b1024;
    for(int i=0;i<5;++i){ C[i].s=P[i]; C[i].c=(WORD)'z';      /* miss -> full scan */
        cs[i].label=N[i]; cs[i].bytes=(size_t)L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrChrA (wia AVX2 vs a byte loop)", cs, 5, 300);
}
