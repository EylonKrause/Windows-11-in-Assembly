// changes/226-pathremoveargsa/bench.c
// Gate 2: time wia_pathremoveargsa against the live shlwapi!PathRemoveArgsA.
// In-place, so each iteration restores the buffer; both sides pay the same restore.
// Lengths are COMPUTED, never hardcoded.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern void wia_pathremoveargsa(char*);
typedef void (WINAPI *FN)(char*);
static FN sys;

typedef struct { const char* src; int n; } CASE;
static char work[640];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  wia_pathremoveargsa(work); return (unsigned char)work[0]; }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  sys(work); return (unsigned char)work[0]; }
#pragma optimize("", on)

static char s16[32], s64[96], s254[300], snoarg[300], squoted[300], strail[300], sreal[128];

static int fill_arg(char* b,int n){          /* a split near the end: behaviours 1 and 2 */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n-6]=' '; b[n-5]=' '; b[n]=0; return n;
}
static int fill_noarg(char* b,int n){        /* no space at all: the scan runs to the terminator
                                                and behaviour 3 finds nothing to trim */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n]=0; return n;
}
static int fill_quoted(char* b,int n){       /* the split is inside quotes until the closing one,
                                                so the carry-less prefix must run the whole way */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[0]='"'; b[4]=' '; b[n-10]='"'; b[n-6]=' ';
    b[n]=0; return n;
}
static int fill_trail(char* b,int n){        /* behaviour 3: a trailing run to trim */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    for(int i=n-5;i<n;i++) b[i]=' ';
    b[n]=0; return n;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"PathRemoveArgsA");
    int n16 = fill_arg(s16,16);
    int n64 = fill_arg(s64,64);
    int n254= fill_arg(s254,254);
    int nna = fill_noarg(snoarg,254);
    int nq  = fill_quoted(squoted,254);
    int nt  = fill_trail(strail,254);
    int nreal;
    {
        static const char* r = "\"C:\\Program Files\\Some App\\app.exe\" --flag value";
        int i=0; for(; r[i]; ++i) sreal[i]=r[i]; sreal[i]=0; nreal=i;
    }

    enum { N = 7 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"16, arg","64, arg","254, arg","254, no arg",
                                  "254, quoted split","254, trailing run","real command line"};
    C[0].src=s16;     C[0].n=n16;
    C[1].src=s64;     C[1].n=n64;
    C[2].src=s254;    C[2].n=n254;
    C[3].src=snoarg;  C[3].n=nna;
    C[4].src=squoted; C[4].n=nq;
    C[5].src=strail;  C[5].n=nt;
    C[6].src=sreal;   C[6].n=nreal;
    static const size_t bytes[] = { 16,64,254,254,254,254,48 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathRemoveArgsA (wia AVX2 + clmul quote parity vs shlwapi scalar)", cs, N, 300);
}
