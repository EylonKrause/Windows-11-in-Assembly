// changes/168-strcpynw/bench.c
// Gate 2: time wia_strcpynw against the live shlwapi!StrCpyNW across size classes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include "bench.h"

extern wchar_t* wia_strcpynw(wchar_t*, const wchar_t*, int);
typedef PWSTR (WINAPI *SCN)(PWSTR, PCWSTR, int);
static SCN sys;

typedef struct { const wchar_t* src; int cch; } CASE;
static wchar_t dst[2200];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; return (uint64_t)(UINT_PTR)wia_strcpynw(dst,k->src,k->cch); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; return (uint64_t)(UINT_PTR)sys(dst,k->src,k->cch); }
#pragma optimize("", on)

static wchar_t s8[16], s16[32], s64[96], s254[300], s1024[1100];
static wchar_t sreal[96];
static void fill(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23)); b[n]=0; }

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (SCN)GetProcAddress(h,"StrCpyNW");
    fill(s8,8); fill(s16,16); fill(s64,64); fill(s254,254); fill(s1024,1024);
    { const wchar_t* r=L"C:\Program Files\Windows NT\Accessories\wordpad.exe";
      int i=0; for(;r[i];++i) sreal[i]=r[i]; sreal[i]=0; }

    enum { N = 7 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"8","16","64","254","1024","realpath","truncate-254to16"};
    C[0].src=s8;    C[0].cch=9;
    C[1].src=s16;   C[1].cch=17;
    C[2].src=s64;   C[2].cch=65;
    C[3].src=s254;  C[3].cch=255;
    C[4].src=s1024; C[4].cch=1025;
    C[5].src=sreal; C[5].cch=64;
    C[6].src=s254;  C[6].cch=16;      /* bound hit long before the terminator */
    static const size_t bytes[] = { 16,32,128,508,2048,98,30 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi StrCpyNW (wia AVX2 fused scan+copy vs shlwapi scalar)", cs, N, 300);
}
