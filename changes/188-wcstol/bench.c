// changes/188-wcstol/bench.c
// Gate 2: time wia_wcstol against the live ucrtbase!wcstol across input shapes and bases.
// The classes cover every route the implementation splits on: ASCII digits, the hex prefix,
// octal auto-detect, base-36 letters, the ERANGE path, and both non-ASCII digit routes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern long wia_wcstol(const wchar_t*, wchar_t**, int);
typedef long (__cdecl *WL)(const wchar_t*, wchar_t**, int);
static WL sys;

typedef struct { const wchar_t* s; int base; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; wchar_t* e;
                                  return (uint64_t)(unsigned)wia_wcstol(k->s,&e,k->base) ^ (uint64_t)(uintptr_t)e; }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; wchar_t* e;
                                  return (uint64_t)(unsigned)sys(k->s,&e,k->base) ^ (uint64_t)(uintptr_t)e; }
#pragma optimize("", on)

static wchar_t fw[32], arab[32], arabhex[32];

int main(void){
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (WL)GetProcAddress(hu,"wcstol");

    { const wchar_t* d=L"2147483647"; int k=0; for(;d[k];k++) fw[k]=(wchar_t)(0xFF10+(d[k]-L'0')); fw[k]=0; }
    { const wchar_t* d=L"2147483647"; int k=0; for(;d[k];k++) arab[k]=(wchar_t)(0x0660+(d[k]-L'0')); arab[k]=0; }
    /* an Arabic-Indic zero introducing a hex prefix, the rule a naive port gets wrong */
    { arabhex[0]=0x0660; arabhex[1]=L'x'; arabhex[2]=L'1'; arabhex[3]=L'a'; arabhex[4]=L'b';
      arabhex[5]=L'c'; arabhex[6]=L'd'; arabhex[7]=L'e'; arabhex[8]=L'f'; arabhex[9]=L'0'; arabhex[10]=0; }

    enum { N = 10 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {
        "\"7\" b10", "\"-987654\" b10", "\"2147483647\" b10", "\"0x1abcdef0\" b0",
        "\"777777\" b8", "\"zzzzzz\" b36", "overflow b10 (ERANGE)",
        "fullwidth 10 digits b10", "Arabic-Indic 10 digits b10", "U+0660 hex prefix b0" };
    C[0].s=L"7";              C[0].base=10;
    C[1].s=L"-987654";        C[1].base=10;
    C[2].s=L"2147483647";     C[2].base=10;
    C[3].s=L"0x1abcdef0";     C[3].base=0;
    C[4].s=L"777777";         C[4].base=8;
    C[5].s=L"zzzzzz";         C[5].base=36;
    C[6].s=L"99999999999999999999"; C[6].base=10;
    C[7].s=fw;                C[7].base=10;
    C[8].s=arab;              C[8].base=10;
    C[9].s=arabhex;           C[9].base=0;
    static const size_t bytes[] = { 1,7,10,10,6,6,20,10,10,10 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase wcstol (wia scalar + AVX2 block classifier vs ucrtbase)", cs, N, 300);
}
