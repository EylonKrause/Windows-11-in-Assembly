// changes/160-strcspn/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern size_t wia_strcspn(const char*, const char*);
typedef size_t (__cdecl *fn)(const char*, const char*);
static fn sys;
typedef struct { const char* s; const char* set; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; return (uint64_t)wia_strcspn(k->s,k->set); }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; return (uint64_t)sys(k->s,k->set); }
#pragma optimize("", on)
static char s16[32], s64[96], s254[300], s1024[1100], searly[300];
static void fill(char* b,int n){ for(int i=0;i<n;i++) b[i]=(char)('a'+(i%3)); b[n]=0; }
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"strcspn");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024);
    memset(searly,'a',254); searly[254]=0; searly[8]='Z';   /* the complement span stops at 8 */
    /* strings are drawn from {a,b,c}: a disjoint set makes the complement span run all the way to the
       terminator, which is the case that has to compare the NUL explicitly */
    static CASE C[6]; static wia_case cs[6];
    static const char* N[]={"16 chars, 3-set","64 chars, 3-set","254 chars, 3-set","1024 chars, 3-set",
                            "254 chars, stops at 8","254 chars, 16-set"};
    static const int L[]={16,64,254,1024,254,254};
    C[0].s=s16; C[1].s=s64; C[2].s=s254; C[3].s=s1024; C[4].s=searly; C[5].s=s254;
    C[0].set=C[1].set=C[2].set=C[3].set="xyz";
    C[4].set="Z";
    C[5].set="defghijklmnopqrs";
    for(int i=0;i<6;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase strcspn (wia AVX2 vs ucrtbase)", cs, 6, 300);
}
