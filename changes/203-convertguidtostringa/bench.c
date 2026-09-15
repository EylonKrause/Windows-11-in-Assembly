// changes/203-convertguidtostringa/bench.c
// Gate 2: time wia_ConvertGuidToStringA against the live iphlpapi!ConvertGuidToStringA.
// The classes cover the generous buffer (the normal case), the exact 39-cell fit, and each of the
// three distinct failure shapes -- because on this function the failures are not rare edge cases,
// they are a third of the contract.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern DWORD wia_ConvertGuidToStringA(const GUID*, PSTR, DWORD);
typedef DWORD (WINAPI *FN)(const GUID*, PSTR, DWORD);
static FN sys;

typedef struct { const GUID* g; DWORD cch; } CASE;
static char obuf[256];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; return (uint64_t)wia_ConvertGuidToStringA(k->g,obuf,k->cch); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; return (uint64_t)sys(k->g,obuf,k->cch); }
#pragma optimize("", on)

static const GUID G1 = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};
static const GUID G2 = {0x00000000,0x0000,0x0000,{0,0,0,0,0,0,0,0}};
static const GUID G3 = {0xFFFFFFFF,0xFFFF,0xFFFF,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}};

int main(void){
    HMODULE h = LoadLibraryW(L"iphlpapi.dll");
    sys = (FN)GetProcAddress(h,"ConvertGuidToStringA");

    enum { N = 6 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {
        "cch 64 (typical)", "cch 39 (exact fit)", "all zero, cch 64", "all FF, cch 64",
        "cch 20 (truncates)", "cch 0 (no write)" };
    C[0].g=&G1; C[0].cch=64;
    C[1].g=&G1; C[1].cch=39;
    C[2].g=&G2; C[2].cch=64;
    C[3].g=&G3; C[3].cch=64;
    C[4].g=&G1; C[4].cch=20;
    C[5].g=&G1; C[5].cch=0;
    static const size_t bytes[] = { 38,38,38,38,19,1 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("iphlpapi ConvertGuidToStringA (wia one vpshufb, no widening, vs a printf engine)", cs, N, 300);
}
