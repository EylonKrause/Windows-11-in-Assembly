// changes/097-rtlintegertochar/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern LONG wia_itoc(ULONG, ULONG, LONG, char*);
typedef LONG (NTAPI *fn)(ULONG, ULONG, LONG, char*);
static fn sys;
typedef struct { ULONG v; ULONG base; } ctx_t;
static char obuf[64];
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)wia_itoc(m->v,m->base,40,obuf); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)sys(m->v,m->base,40,obuf); }
#pragma optimize("", on)
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIntegerToChar");
    // representative: decimal formatting of values with 1,3,5,10 digits + a hex case
    static ctx_t C[]={{5,10},{123,10},{54321,10},{4000000000u,10},{0xDEADBEEF,16},{0xFFFFFFFF,2}};
    static const char* N[]={"dec-1d","dec-3d","dec-5d","dec-10d","hex-8d","bin-32d"};
    enum{K=6}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=0; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("RtlIntegerToChar (wia 2-digit table vs ntdll scalar)", cs, K, 200);
}
