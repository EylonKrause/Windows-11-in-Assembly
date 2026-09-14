// changes/172-pathquotespacesw/bench.c
// Gate 2: time wia_pathquotespacesw against the live shlwapi!PathQuoteSpacesW.
// In-place and it grows, so each iteration restores the buffer; both sides pay the same restore.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "bench.h"

extern int wia_pathquotespacesw(wchar_t*);
typedef BOOL (WINAPI *PQS)(LPWSTR);
static PQS sys;

typedef struct { const wchar_t* src; int n; } CASE;
static wchar_t work[900];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2);
                                  return (uint64_t)wia_pathquotespacesw(work); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2);
                                  return (uint64_t)sys(work); }
#pragma optimize("", on)

static wchar_t s8[16], s16[32], s64[96], s254[300], sreal[96], nospace[300], toolong[600];

static void fill_sp(wchar_t* b,int n){
    for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23));
    if(n>1) b[n/2]=L' ';
    b[n]=0;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PQS)GetProcAddress(h,"PathQuoteSpacesW");
    fill_sp(s8,8); fill_sp(s16,16); fill_sp(s64,64); fill_sp(s254,254);
    int nreal;
    {
        static const wchar_t* r = L"C:\\Program Files\\Windows NT\\Accessories\\wordpad.exe";
        int i=0; for(; r[i]; ++i) sreal[i]=r[i]; sreal[i]=0; nreal=i;
    }
    for(int i=0;i<254;i++) nospace[i]=(wchar_t)(L'a'+(i%23));
    nospace[254]=0;                                  /* no space -> FALSE, untouched */
    for(int i=0;i<300;i++) toolong[i]=(wchar_t)(L'a'+(i%23));
    toolong[150]=L' '; toolong[300]=0;               /* has a space but exceeds MAX_PATH */

    enum { N = 7 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"8","16","64","254","realpath","254/no-space","300/too-long"};
    C[0].src=s8;      C[0].n=8;
    C[1].src=s16;     C[1].n=16;
    C[2].src=s64;     C[2].n=64;
    C[3].src=s254;    C[3].n=254;
    C[4].src=sreal;   C[4].n=nreal;   /* computed: hardcoding it once cost a terminator and corrupted the measurement */
    C[5].src=nospace; C[5].n=254;
    C[6].src=toolong; C[6].n=300;
    static const size_t bytes[] = { 16,32,128,508,100,508,600 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathQuoteSpacesW (wia AVX2 one-pass scan + vector insert vs shlwapi scalar)", cs, N, 300);
}
