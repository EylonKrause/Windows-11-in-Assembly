// changes/137-strpbrkw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern const wchar_t* wia_strpbrkw(const wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *fn)(const wchar_t*, const wchar_t*);
static fn sys;
typedef struct { const wchar_t* s; const wchar_t* set; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)wia_strpbrkw(k->s,k->set); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)sys(k->s,k->set); }
#pragma optimize("", on)
static wchar_t s16[32], s64[96], s254[300], s1024[1100], shit[300];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=L'a'+(i%23); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrPBrkW");
    fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024); fill(shit,254);
    shit[200]=L'X';
    static const wchar_t* SET3=L"XYZ";              /* disjoint -> full scan */
    static const wchar_t* SET12=L"XYZ0123456789";   /* larger set, still disjoint */
    static CASE C[5]; static wia_case cs[5];
    static const char* N[]={"16/set3","64/set3","254/set3","1024/set3","254/set13-hit@200"};
    static const int L[]={16,64,254,1024,254};
    C[0].s=s16;   C[0].set=SET3;
    C[1].s=s64;   C[1].set=SET3;
    C[2].s=s254;  C[2].set=SET3;
    C[3].s=s1024; C[3].set=SET3;
    C[4].s=shit;  C[4].set=SET12;
    for(int i=0;i<5;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrPBrkW (wia AVX2 vs shlwapi)", cs, 5, 200);
}
