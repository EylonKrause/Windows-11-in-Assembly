// changes/129-rtlchartointeger/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern long wia_char2int(const char*, unsigned long, unsigned long*);
typedef long (WINAPI *fn)(const char*, unsigned long, unsigned long*);
static fn sys;
typedef struct { const char* s; unsigned long base; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; unsigned long v=0; wia_char2int(k->s,k->base,&v); return v; }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; unsigned long v=0; sys(k->s,k->base,&v); return v; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlCharToInteger");
    static CASE C[]={ {"1234567890",10}, {"0xDEADBEEF",0}, {"42",10}, {"  -2147483648",0} };
    static const char* N[]={"10digits","hexprefix","short","ws+neg"};
    enum{K=4}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=8; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ntdll RtlCharToInteger (wia vs ntdll)", cs, K, 300);
}
