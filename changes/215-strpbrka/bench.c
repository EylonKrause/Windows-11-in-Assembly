// changes/215-strpbrka/bench.c
// Gate 2: time wia_strpbrka against the live shlwapi!StrPBrkA.
//
// The same classes change 137 used for the wide form, at the same CHARACTER counts, so the two are
// directly comparable: four disjoint-set full scans of growing length, and one larger set with an
// early hit. The 16-character class is the one that has to carry the bitmap build, which is the only
// fixed cost this implementation has.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern const char* wia_strpbrka(const char*, const char*);
typedef char* (WINAPI *fn)(const char*, const char*);
static fn sys;
typedef struct { const char* s; const char* set; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)wia_strpbrka(k->s,k->set); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)sys(k->s,k->set); }
#pragma optimize("", on)
static char s16[32], s64[96], s254[300], s1024[1100], shit[300];
static void fill(char* b,int n){ for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23)); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrPBrkA");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024); fill(shit,254);
    shit[200]='X';
    static const char* SET3="XYZ";              /* disjoint -> full scan */
    static const char* SET12="XYZ0123456789";   /* larger set, still disjoint */
    static CASE C[5]; static wia_case cs[5];
    static const char* N[]={"16/set3","64/set3","254/set3","1024/set3","254/set13-hit@200"};
    static const int L[]={16,64,254,1024,254};
    C[0].s=s16;   C[0].set=SET3;
    C[1].s=s64;   C[1].set=SET3;
    C[2].s=s254;  C[2].set=SET3;
    C[3].s=s1024; C[3].set=SET3;
    C[4].s=shit;  C[4].set=SET12;
    for(int i=0;i<5;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrPBrkA (wia 256-bit set + AVX2 vs an MBCS per-character walk)", cs, 5, 300);
}
