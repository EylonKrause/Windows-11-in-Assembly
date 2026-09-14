// changes/169-strchrnw/bench.c
// Gate 2: time wia_strchrnw against the live shlwapi!StrChrNW across size classes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include "bench.h"

extern wchar_t* wia_strchrnw(const wchar_t*, wchar_t, unsigned int);
typedef PWSTR (WINAPI *SCNW)(PCWSTR, WCHAR, UINT);
static SCNW sys;

typedef struct { const wchar_t* s; wchar_t m; unsigned n; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; return (uint64_t)(UINT_PTR)wia_strchrnw(k->s,k->m,k->n); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; return (uint64_t)(UINT_PTR)sys(k->s,k->m,k->n); }
#pragma optimize("", on)

static wchar_t s16[32], s64[96], s254[300], s1024[1100];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23)); b[n]=0; }

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (SCNW)GetProcAddress(h,"StrChrNW");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024);

    enum { N = 7 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"16/miss","64/miss","254/miss","1024/miss","254/hit@200","254/hit@2","254/bound16"};
    C[0].s=s16;   C[0].m=L'Z'; C[0].n=17;
    C[1].s=s64;   C[1].m=L'Z'; C[1].n=65;
    C[2].s=s254;  C[2].m=L'Z'; C[2].n=255;
    C[3].s=s1024; C[3].m=L'Z'; C[3].n=1025;
    C[4].s=s254;  C[4].m=s254[200]; C[4].n=255;   /* hit late */
    C[5].s=s254;  C[5].m=s254[2];   C[5].n=255;   /* hit immediately */
    C[6].s=s254;  C[6].m=L'Z'; C[6].n=16;         /* bound cuts the scan short */
    static const size_t bytes[] = { 32,128,508,2048,402,6,32 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrChrNW (wia AVX2 dual-compare vs shlwapi scalar)", cs, N, 300);
}
