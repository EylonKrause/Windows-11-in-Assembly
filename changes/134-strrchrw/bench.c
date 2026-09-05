// changes/134-strrchrw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern const wchar_t* wia_strrchrw(const wchar_t*, const wchar_t*, wchar_t);
typedef wchar_t* (WINAPI *fn)(const wchar_t*, const wchar_t*, wchar_t);
static fn sys;
typedef struct { const wchar_t* s; const wchar_t* e; wchar_t c; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)wia_strrchrw(k->s,k->e,k->c); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)sys(k->s,k->e,k->c); }
#pragma optimize("", on)
static wchar_t h64[96], h254[300], h1024[1100], hp[300];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=L'a'+(i%23); b[n]=0; }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrRChrW");
    fill(h64,64); fill(h254,254); fill(h1024,1024); fill(hp,254);
    hp[240]=(wchar_t)0x5C;                       /* backslash, as in a path */
    static CASE C[5]; static wia_case cs[5];
    static const char* N[]={"64/miss/unb","254/miss/unb","1024/miss/unb","254/bounded","254/hit@240"};
    static const int L[]={64,254,1024,254,254};
    C[0].s=h64;   C[0].e=0;        C[0].c=L'#';
    C[1].s=h254;  C[1].e=0;        C[1].c=L'#';
    C[2].s=h1024; C[2].e=0;        C[2].c=L'#';
    C[3].s=h254;  C[3].e=h254+254; C[3].c=L'#';
    C[4].s=hp;    C[4].e=0;        C[4].c=(wchar_t)0x5C;
    for(int i=0;i<5;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrRChrW (wia AVX2 vs shlwapi)", cs, 5, 300);
}
