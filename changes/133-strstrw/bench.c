// changes/133-strstrw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern const wchar_t* wia_strstrw(const wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *fn)(const wchar_t*, const wchar_t*);
static fn sys;
typedef struct { const wchar_t* h; const wchar_t* n; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)wia_strstrw(k->h,k->n); }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; return (uint64_t)(size_t)sys(k->h,k->n); }
#pragma optimize("", on)
static wchar_t h16[32], h64[96], h254[300], h1024[1100], hhit[300];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=L'a'+(i%23); b[n]=0; }
int main(void){
    HMODULE mh=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(mh,"StrStrW");
    fill(h16,16); fill(h64,64); fill(h254,254); fill(h1024,1024); fill(hhit,254);
    hhit[200]=L'q'; hhit[201]=L'q'; hhit[202]=L'q'; hhit[203]=L'q';
    static CASE C[5]; static wia_case cs[5];
    static const char* N[]={"16/miss","64/miss","254/miss","1024/miss","254/hit@200"};
    static const int L[]={16,64,254,1024,254};
    C[0].h=h16; C[1].h=h64; C[2].h=h254; C[3].h=h1024; C[4].h=hhit;
    C[0].n=L"zzzz"; C[1].n=L"zzzz"; C[2].n=L"zzzz"; C[3].n=L"zzzz"; C[4].n=L"qqqq";
    for(int i=0;i<5;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrStrW (wia AVX2 vs shlwapi)", cs, 5, 300);
}
