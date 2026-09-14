// changes/187-wtoi64/bench.c
// Gate 2: time wia_wtoi64 against the live ucrtbase!_wtoi64 across input shapes.
// The classes cover the frequency split the implementation is built around: ASCII digits, a
// leading whitespace run, both saturation paths, and the two non-ASCII digit routes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern __int64 wia_wtoi64(const wchar_t*);
typedef __int64 (__cdecl *W64)(const wchar_t*);
static W64 sys;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ return (uint64_t)wia_wtoi64((const wchar_t*)c); }
static uint64_t op_sys (void* c){ return (uint64_t)sys((const wchar_t*)c); }
#pragma optimize("", on)

static wchar_t fw[40], arab[40], longz[80];

int main(void){
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (W64)GetProcAddress(hu,"_wtoi64");

    { const wchar_t* d=L"9223372036854775807"; int k=0;
      for(;d[k];k++) fw[k]=(wchar_t)(0xFF10+(d[k]-L'0')); fw[k]=0; }
    { const wchar_t* d=L"9223372036854775807"; int k=0;
      for(;d[k];k++) arab[k]=(wchar_t)(0x0660+(d[k]-L'0')); arab[k]=0; }
    { int k=0; for(;k<32;k++) longz[k]=L'0'; longz[k++]=L'4'; longz[k++]=L'2'; longz[k]=0; }

    enum { N = 8 };
    static wia_case cs[N];
    static const char* names[] = {
        "\"42\"", "\"-1234567890\"", "19 digits (_I64_MAX)", "\"-9223372036854775808\"",
        "26 nines (saturates)", "32 zeros + \"42\"", "fullwidth 19 digits", "Arabic-Indic 19 digits" };
    static const wchar_t* in[N];
    in[0]=L"42";
    in[1]=L"-1234567890";
    in[2]=L"9223372036854775807";
    in[3]=L"-9223372036854775808";
    in[4]=L"99999999999999999999999999";
    in[5]=longz;
    in[6]=fw;
    in[7]=arab;
    static const size_t bytes[] = { 2,11,19,20,26,34,19,19 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=(void*)in[i]; }
    return wia_bench_compare("ucrtbase _wtoi64 (wia frameless saturating scalar + AVX2 block classifier vs ucrtbase)", cs, N, 300);
}
