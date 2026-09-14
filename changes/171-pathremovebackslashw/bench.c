// changes/171-pathremovebackslashw/bench.c
// Gate 2: time wia_pathremovebackslashw against the live shlwapi!PathRemoveBackslashW.
// In-place, so each iteration restores the buffer; both sides pay the same restore.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "bench.h"

extern wchar_t* wia_pathremovebackslashw(wchar_t*);
typedef PWSTR (WINAPI *PRB)(PWSTR);
static PRB sys;

typedef struct { const wchar_t* src; int n; } CASE;
static wchar_t work[1200];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2);
                                  return (uint64_t)(UINT_PTR)wia_pathremovebackslashw(work); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2);
                                  return (uint64_t)(UINT_PTR)sys(work); }
#pragma optimize("", on)

static wchar_t s4[8], s16[32], s64[96], s254[300], s1024[1100], sreal[96], sroot[8];

static void fill(wchar_t* b, int n){
    for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23));
    if(n>0) b[n-1]=L'\\';          /* every case ends with a backslash to remove */
    b[n]=0;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PRB)GetProcAddress(h,"PathRemoveBackslashW");
    fill(s4,4); fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024);
    int nreal;
    {
        static const wchar_t* r = L"C:\\Program Files\\Windows NT\\Accessories\\";
        int i=0; for(; r[i]; ++i) sreal[i]=r[i]; sreal[i]=0; nreal=i;
    }
    sroot[0]=L'C'; sroot[1]=L':'; sroot[2]=L'\\'; sroot[3]=0;

    enum { N = 7 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"4","16","64","254","1024","realpath","drive-root"};
    C[0].src=s4;    C[0].n=4;
    C[1].src=s16;   C[1].n=16;
    C[2].src=s64;   C[2].n=64;
    C[3].src=s254;  C[3].n=254;
    C[4].src=s1024; C[4].n=1024;
    C[5].src=sreal; C[5].n=nreal;   /* computed, not hardcoded */
    C[6].src=sroot; C[6].n=3;      /* protected: nothing is removed */
    static const size_t bytes[] = { 8,32,128,508,2048,78,6 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathRemoveBackslashW (wia AVX2 length scan vs shlwapi scalar)", cs, N, 300);
}
