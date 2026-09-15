// changes/213-strrchra/bench.c
// Gate 2: time wia_strrchra against the live shlwapi!StrRChrA.
//
// The same classes change 134 used for the wide form, at the same CHARACTER counts, so the two are
// directly comparable: misses of three lengths through the unbounded path, one bounded range, and
// the "last backslash in a path" hit that is what callers actually do.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern const char* wia_strrchra(const char*, const char*, WORD);
typedef char* (WINAPI *fn)(const char*, const char*, WORD);
static fn sys;
typedef struct { const char* s; const char* e; WORD c; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)wia_strrchra(k->s,k->e,k->c); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)sys(k->s,k->e,k->c); }
#pragma optimize("", on)
static char h64[96], h254[300], h1024[1100], hp[300];
static void fill(char* b,int n){ for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23)); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrRChrA");
    fill(h64,64); fill(h254,254); fill(h1024,1024); fill(hp,254);
    hp[240]=(char)0x5C;                          /* backslash, as in a path */
    static CASE C[5]; static wia_case cs[5];
    static const char* N[]={"64/miss/unb","254/miss/unb","1024/miss/unb","254/bounded","254/hit@240"};
    static const int L[]={64,254,1024,254,254};
    C[0].s=h64;   C[0].e=0;        C[0].c='#';
    C[1].s=h254;  C[1].e=0;        C[1].c='#';
    C[2].s=h1024; C[2].e=0;        C[2].c='#';
    C[3].s=h254;  C[3].e=h254+254; C[3].c='#';
    C[4].s=hp;    C[4].e=0;        C[4].c=(WORD)0x5C;
    for(int i=0;i<5;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrRChrA (wia AVX2 vs a CharNextA-per-character walk)", cs, 5, 300);
}
