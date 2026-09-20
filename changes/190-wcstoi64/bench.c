// changes/190-wcstoi64/bench.c
// Gate 2: time wia_wcstoi64 against the live ucrtbase!_wcstoi64 across input shapes and bases.
// The classes cover every route the implementation splits on: ASCII digits, the hex prefix,
// octal auto-detect, base-36 letters, the ERANGE path, and both non-ASCII digit routes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern __int64 wia_wcstoi64(const wchar_t*, wchar_t**, int);
typedef __int64 (__cdecl *WL)(const wchar_t*, wchar_t**, int);
static WL sys;

typedef struct { const wchar_t* s; int base; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; wchar_t* e;
                                  return (uint64_t)wia_wcstoi64(k->s,&e,k->base) ^ (uint64_t)(uintptr_t)e; }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; wchar_t* e;
                                  return (uint64_t)sys(k->s,&e,k->base) ^ (uint64_t)(uintptr_t)e; }
#pragma optimize("", on)

static wchar_t fw[40], arab[40], arabhex[40];

int main(void){
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (WL)GetProcAddress(hu,"_wcstoi64");

    { const wchar_t* d=L"9223372036854775807"; int k=0;
      for(;d[k];k++) fw[k]=(wchar_t)(0xFF10+(d[k]-L'0')); fw[k]=0; }
    { const wchar_t* d=L"9223372036854775807"; int k=0;
      for(;d[k];k++) arab[k]=(wchar_t)(0x0660+(d[k]-L'0')); arab[k]=0; }
    /* an Arabic-Indic zero introducing a hex prefix, the rule a naive port gets wrong */
    { const wchar_t* d=L"x1abcdef01234567"; arabhex[0]=0x0660;
      int k=0; for(;d[k];k++) arabhex[k+1]=d[k]; arabhex[k+1]=0; }

    enum { N = 10 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {
        "\"7\" b10", "\"-987654\" b10", "19 digits b10", "\"0x1abcdef012345678\" b0",
        "\"777777\" b8", "\"zzzzzzzzzzz\" b36", "26 nines b10 (ERANGE)",
        "fullwidth 19 digits b10", "Arabic-Indic 19 digits b10", "U+0660 hex prefix b0" };
    C[0].s=L"7";                       C[0].base=10;
    C[1].s=L"-987654";                 C[1].base=10;
    C[2].s=L"9223372036854775807";     C[2].base=10;
    C[3].s=L"0x1abcdef012345678";      C[3].base=0;
    C[4].s=L"777777";                  C[4].base=8;
    C[5].s=L"zzzzzzzzzzz";             C[5].base=36;
    C[6].s=L"99999999999999999999999999"; C[6].base=10;
    C[7].s=fw;                         C[7].base=10;
    C[8].s=arab;                       C[8].base=10;
    C[9].s=arabhex;                    C[9].base=0;
    static const size_t bytes[] = { 1,7,19,18,6,11,26,19,19,17 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase _wcstoi64 (wia scalar + AVX2 block classifier vs ucrtbase)", cs, N, 300);
}
