// changes/158-wcspbrk/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include "bench.h"
extern const wchar_t* wia_wcspbrk(const wchar_t*, const wchar_t*);
typedef wchar_t* (__cdecl *fn)(const wchar_t*, const wchar_t*);
static fn sys;
typedef struct { const wchar_t* s; const wchar_t* set; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; return (uint64_t)(size_t)wia_wcspbrk(k->s,k->set); }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; return (uint64_t)(size_t)sys(k->s,k->set); }
#pragma optimize("", on)
static wchar_t s16[32], s64[96], s254[300], s1024[1100], searly[300];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%3)); b[n]=0; }
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"wcspbrk");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024);
    /* strings are drawn from {a,b,c}: a disjoint set means the scan runs to the terminator (NULL) */
    for(int i=0;i<254;i++) searly[i]=L'a';
    searly[254]=0; searly[8]=L'Z';        /* an early hit, but far enough in to be measurable */
    static CASE C[6]; static wia_case cs[6];
    static const char* N[]={"16 chars, 3-set","64 chars, 3-set","254 chars, 3-set","1024 chars, 3-set",
                            "254 chars, hit at 8","254 chars, 16-set"};
    static const int L[]={16,64,254,1024,254,254};
    C[0].s=s16; C[1].s=s64; C[2].s=s254; C[3].s=s1024; C[4].s=searly; C[5].s=s254;
    C[0].set=C[1].set=C[2].set=C[3].set=L"xyz";
    /* "hit at 8" rather than "hit at 0": a do-nothing INDIRECT CALL costs 3.11 ns in this harness and
       the live routine measures 3.12 ns on a hit at index 0, so that input times the call and not the
       code. It is still measured and reported in RESULTS.md -- it just cannot serve as a gate. */
    C[4].set=L"Z";
    C[5].set=L"defghijklmnopqrs";
    for(int i=0;i<6;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase wcspbrk (wia AVX2 vs ucrtbase)", cs, 6, 300);
}
