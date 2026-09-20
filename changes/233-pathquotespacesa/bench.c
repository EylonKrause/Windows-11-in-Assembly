// changes/233-pathquotespacesa/bench.c
// Gate 2: time wia_pathquotespacesa against the live shlwapi!PathQuoteSpacesA.
// In-place, so each iteration restores the buffer; both sides pay the same restore.
// Lengths are COMPUTED, never hardcoded.
//
// The case mix. The function has two very different outcomes: quote (scan + a shift of up to 257
// bytes) and refuse (scan only, nothing written). Both are common, most paths have no space --
// so both are measured, and the over-length row is there because the 257 cap turns a long path into
// the refuse case no matter what it contains.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int wia_pathquotespacesa(char*);
typedef int (WINAPI *FN)(char*);
static FN sys;

typedef struct { const char* src; int n; } CASE;
static char work[700];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  return (uint64_t)wia_pathquotespacesa(work); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  return (uint64_t)sys(work); }
#pragma optimize("", on)

static char s16[32], s64[96], s257[300], sno64[96], sno254[300], sover[400], sreal[128];

static int fill_sp(char* b,int n){          /* has a space: the quoting path */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n/2]=' '; b[n]=0; return n;
}
static int fill_nosp(char* b,int n){        /* no space: scan, then refuse */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n]=0; return n;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"PathQuoteSpacesA");
    int n16  = fill_sp(s16,16);
    int n64  = fill_sp(s64,64);
    int n257 = fill_sp(s257,257);               /* exactly at the cap */
    int nn64 = fill_nosp(sno64,64);
    int nn254= fill_nosp(sno254,254);
    int nov  = fill_sp(sover,300);              /* has a space but is over the cap */
    int nreal;
    {
        static const char* r = "C:\\Program Files\\Some Vendor\\app.exe";
        int i=0; for(; r[i]; ++i) sreal[i]=r[i]; sreal[i]=0; nreal=i;
    }

    enum { N = 7 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"16, space","64, space","257, space (at the cap)",
                                  "64, no space","254, no space","300, space but OVER the cap",
                                  "real path with spaces"};
    C[0].src=s16;    C[0].n=n16;
    C[1].src=s64;    C[1].n=n64;
    C[2].src=s257;   C[2].n=n257;
    C[3].src=sno64;  C[3].n=nn64;
    C[4].src=sno254; C[4].n=nn254;
    C[5].src=sover;  C[5].n=nov;
    C[6].src=sreal;  C[6].n=nreal;
    static const size_t bytes[] = { 16,64,257,64,254,300,35 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathQuoteSpacesA (wia AVX2 fused scan + chunked shift vs shlwapi)", cs, N, 300);
}
