// changes/232-pathremovebackslasha/bench.c
// Gate 2: time wia_pathremovebackslasha against the live shlwapi!PathRemoveBackslashA.
// In-place, so each iteration restores the buffer; both sides pay the same restore.
// Lengths are COMPUTED, never hardcoded.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern char* wia_pathremovebackslasha(char*);
typedef char* (WINAPI *FN)(char*);
static FN sys;

typedef struct { const char* src; int n; } CASE;
static char work[1300];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  return (uint64_t)(size_t)wia_pathremovebackslasha(work); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  return (uint64_t)(size_t)sys(work); }
#pragma optimize("", on)

static char s16[32], s64[96], s254[300], s1k[1100], snob[300], sroot[16], sreal[128];

static int fill_bs(char* b,int n){          /* a trailing backslash: the strip path */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n-1]='\\'; b[n]=0; return n;
}
static int fill_nobs(char* b,int n){        /* no trailing backslash: the scan, then nothing */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n]=0; return n;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"PathRemoveBackslashA");
    int n16 = fill_bs(s16,16);
    int n64 = fill_bs(s64,64);
    int n254= fill_bs(s254,254);
    int n1k = fill_bs(s1k,1024);
    int nnb = fill_nobs(snob,254);
    int nrt; { const char* r = "C:\\"; int i=0; for(; r[i]; ++i) sroot[i]=r[i]; sroot[i]=0; nrt=i; }
    int nreal;
    {
        static const char* r = "C:\\Users\\eylonk\\Documents\\GitHub\\project\\src\\";
        int i=0; for(; r[i]; ++i) sreal[i]=r[i]; sreal[i]=0; nreal=i;
    }

    enum { N = 7 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"16, trailing \\","64, trailing \\","254, trailing \\",
                                  "1024, trailing \\","254, none","C:\\ (protected root)",
                                  "real path"};
    C[0].src=s16;   C[0].n=n16;
    C[1].src=s64;   C[1].n=n64;
    C[2].src=s254;  C[2].n=n254;
    C[3].src=s1k;   C[3].n=n1k;
    C[4].src=snob;  C[4].n=nnb;
    C[5].src=sroot; C[5].n=nrt;
    C[6].src=sreal; C[6].n=nreal;
    static const size_t bytes[] = { 16,64,254,1024,254,3,44 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathRemoveBackslashA (wia AVX2 scan + the root rule vs shlwapi)", cs, N, 300);
}
