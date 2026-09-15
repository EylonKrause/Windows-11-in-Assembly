// changes/223-pathundecoratea/bench.c
// Gate 2: time wia_pathundecoratea against the live shlwapi!PathUndecorateA.
// In-place, so each iteration restores the buffer; both sides pay the same restore.
// Lengths are COMPUTED, never hardcoded -- a hardcoded length once cost a terminator and
// silently corrupted this project's measurements (see change 172's RESULTS.md).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern void wia_pathundecoratea(char*);
typedef void (WINAPI *PUD)(PSTR);
static PUD sys;

typedef struct { const char* src; int n; } CASE;
static char work[1300];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  wia_pathundecoratea(work); return (unsigned char)work[0]; }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  sys(work); return (unsigned char)work[0]; }
#pragma optimize("", on)

static char dec16[32], dec64[96], dec254[300], dec1024[1100], plain254[300], spc254[300], sreal[96];

static int fill_dec(char* b,int n){             /* "...[1].txt" -- a decoration that IS removed */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n-8]='['; b[n-7]='1'; b[n-6]=']'; b[n-5]='.';
    b[n]=0; return n;
}
static int fill_plain(char* b,int n){           /* no decoration at all -- full scan, no move */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n-4]='.'; b[n]=0; return n;
}
static int fill_spc(char* b,int n){             /* a SPACE mid-path: the second tracked position
                                                   is live, and the decoration still goes */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n/2]=' ';
    b[n-8]='['; b[n-7]='1'; b[n-6]=']'; b[n-5]='.';
    b[n]=0; return n;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PUD)GetProcAddress(h,"PathUndecorateA");
    int n16  = fill_dec(dec16,16);
    int n64  = fill_dec(dec64,64);
    int n254 = fill_dec(dec254,254);
    int n1k  = fill_dec(dec1024,1024);
    int np   = fill_plain(plain254,254);
    int nsp  = fill_spc(spc254,254);
    int nreal;
    {
        static const char* r = "C:\\Users\\eylonk\\Downloads\\installer[1].exe";
        int i=0; for(; r[i]; ++i) sreal[i]=r[i]; sreal[i]=0; nreal=i;
    }

    enum { N = 7 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"16/dec","64/dec","254/dec","1024/dec","254/no-dec",
                                  "254/dec+space","realpath"};
    C[0].src=dec16;    C[0].n=n16;
    C[1].src=dec64;    C[1].n=n64;
    C[2].src=dec254;   C[2].n=n254;
    C[3].src=dec1024;  C[3].n=n1k;
    C[4].src=plain254; C[4].n=np;
    C[5].src=spc254;   C[5].n=nsp;
    C[6].src=sreal;    C[6].n=nreal;
    static const size_t bytes[] = { 16,64,254,1024,254,254,40 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathUndecorateA (wia AVX2 fused four-way scan vs shlwapi scalar)", cs, N, 300);
}
