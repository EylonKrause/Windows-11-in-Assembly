// changes/174-pathundecoratew/bench.c
// Gate 2: time wia_pathundecoratew against the live shlwapi!PathUndecorateW.
// In-place, so each iteration restores the buffer; both sides pay the same restore.
// Lengths are COMPUTED, never hardcoded; a hardcoded length once cost a terminator and
// silently corrupted this project's measurements (see change 172's RESULTS.md).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "bench.h"

extern void wia_pathundecoratew(wchar_t*);
typedef void (WINAPI *PUD)(PWSTR);
static PUD sys;

typedef struct { const wchar_t* src; int n; } CASE;
static wchar_t work[1300];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2);
                                  wia_pathundecoratew(work); return work[0]; }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2);
                                  sys(work); return work[0]; }
#pragma optimize("", on)

static wchar_t dec16[32], dec64[96], dec254[300], dec1024[1100], plain254[300], sreal[96];

static int fill_dec(wchar_t* b,int n){          /* "...[1].txt" -- a decoration that IS removed */
    for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23));
    b[n-8]=L'['; b[n-7]=L'1'; b[n-6]=L']'; b[n-5]=L'.';
    b[n]=0; return n;
}
static int fill_plain(wchar_t* b,int n){        /* no decoration at all -- full scan, no move */
    for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23));
    b[n-4]=L'.'; b[n]=0; return n;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PUD)GetProcAddress(h,"PathUndecorateW");
    int n16  = fill_dec(dec16,16);
    int n64  = fill_dec(dec64,64);
    int n254 = fill_dec(dec254,254);
    int n1k  = fill_dec(dec1024,1024);
    int np   = fill_plain(plain254,254);
    int nreal;
    {
        static const wchar_t* r = L"C:\\Users\\eylonk\\Downloads\\installer[1].exe";
        int i=0; for(; r[i]; ++i) sreal[i]=r[i]; sreal[i]=0; nreal=i;
    }

    enum { N = 6 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"16/dec","64/dec","254/dec","1024/dec","254/no-dec","realpath"};
    C[0].src=dec16;    C[0].n=n16;
    C[1].src=dec64;    C[1].n=n64;
    C[2].src=dec254;   C[2].n=n254;
    C[3].src=dec1024;  C[3].n=n1k;
    C[4].src=plain254; C[4].n=np;
    C[5].src=sreal;    C[5].n=nreal;
    static const size_t bytes[] = { 32,128,508,2048,508,80 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathUndecorateW (wia AVX2 fused three-way scan vs shlwapi scalar)", cs, N, 300);
}
