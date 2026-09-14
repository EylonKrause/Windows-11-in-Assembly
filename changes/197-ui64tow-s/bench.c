// changes/197-ui64tow-s/bench.c
// Gate 2: time wia_i64toa_s against the live ucrtbase!_i64toa_s.
// The classes cover the two radixes that get a division-free path (10 and 16), a radix that does
// not (36), and the ERANGE path, which has to reproduce ucrtbase's reversed leftovers exactly.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern int wia_ui64tow_s(unsigned long long, wchar_t*, size_t, int);
typedef int (__cdecl *FN)(unsigned long long, wchar_t*, size_t, int);
static FN sys;

static void __cdecl silent(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                           unsigned d, uintptr_t e){ (void)a;(void)b;(void)c;(void)d;(void)e; }

typedef struct { unsigned long long v; size_t size; int radix; } CASE;
static wchar_t obuf[128];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; return (uint64_t)wia_ui64tow_s(k->v,obuf,k->size,k->radix); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; return (uint64_t)sys(k->v,obuf,k->size,k->radix); }
#pragma optimize("", on)

int main(void){
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (FN)GetProcAddress(hu,"_ui64tow_s");
    { typedef void*(__cdecl*S)(void*); S s=(S)GetProcAddress(hu,"_set_invalid_parameter_handler");
      if(s) s((void*)silent); }

    enum { N = 8 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {
        "42 b10", "1234567890 b10", "19 digits b10", "_UI64_MAX b10",
        "0x7fffffffffffffff b16", "64 bits b2", "19 digits b36", "19 digits b10 ERANGE" };
    C[0].v=42;                      C[0].size=32; C[0].radix=10;
    C[1].v=1234567890ULL;           C[1].size=32; C[1].radix=10;
    C[2].v=9223372036854775807ULL;  C[2].size=32; C[2].radix=10;
    C[3].v=18446744073709551615ULL; C[3].size=32; C[3].radix=10;
    C[4].v=9223372036854775807ULL;  C[4].size=32; C[4].radix=16;
    C[5].v=~0ULL;                   C[5].size=80; C[5].radix=2;
    C[6].v=9223372036854775807ULL;  C[6].size=32; C[6].radix=36;
    C[7].v=9223372036854775807ULL;  C[7].size=12; C[7].radix=10;
    static const size_t bytes[] = { 2,11,19,20,16,64,13,12 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase _ui64tow_s (wia wide table/shift digits vs ucrtbase div-per-digit)", cs, N, 300);
}
