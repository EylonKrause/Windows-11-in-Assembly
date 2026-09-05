// changes/100-rtllargeintegertochar/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern LONG wia_litoc(LARGE_INTEGER*, ULONG, LONG, char*);
typedef LONG (NTAPI *fn)(LARGE_INTEGER*, ULONG, LONG, char*);
static fn sys;
typedef struct { LARGE_INTEGER v; ULONG base; } ctx_t;
static char obuf[96];
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)wia_litoc(&m->v,m->base,72,obuf); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)sys(&m->v,m->base,72,obuf); }
#pragma optimize("", on)
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlLargeIntegerToChar");
    static ctx_t C[6];
    C[0].v.QuadPart=5;                       C[0].base=10;
    C[1].v.QuadPart=54321;                   C[1].base=10;
    C[2].v.QuadPart=1234567890123LL;         C[2].base=10;
    C[3].v.QuadPart=(LONGLONG)0xFFFFFFFFFFFFFFFFULL; C[3].base=10;   // 20 decimal digits
    C[4].v.QuadPart=(LONGLONG)0xFEDCBA9876543210ULL; C[4].base=16;   // 16 hex digits
    C[5].v.QuadPart=(LONGLONG)0xFFFFFFFFFFFFFFFFULL; C[5].base=2;     // 64 binary digits
    static const char* N[]={"dec-1d","dec-5d","dec-13d","dec-20d","hex-16d","bin-64d"};
    enum{K=6}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=0; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("RtlLargeIntegerToChar (wia 2-digit table + direct hex/bin vs ntdll scalar)", cs, K, 200);
}
