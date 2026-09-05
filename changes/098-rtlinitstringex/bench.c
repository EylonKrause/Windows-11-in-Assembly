// changes/094-rtlinitunicodestring/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { USHORT Length; USHORT MaximumLength; PCHAR Buffer; } STRING, *PSTRING;
extern LONG wia_rtlinitstrex(PSTRING, const char*);
typedef LONG (NTAPI *fn)(PSTRING, const char*);
static fn sys;
typedef struct { const char* s; STRING us; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; wia_rtlinitstrex(&m->us,m->s); return m->us.Length; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; sys(&m->us,m->s); return m->us.Length; }
#pragma optimize("", on)
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlInitStringEx");
    // realistic name/path lengths (RtlInitString is dominated by short strings)
    static const int L[]={4,8,16,32,64,260};
    static const char* N[]={"4","8","16","32","64","260"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        int n=L[i];
        char* s=(char*)malloc(n+1);
        for(int k=0;k<n;k++) s[k]=(char)('a'+(k%26));
        s[n]=0;
        cx[i].s=s;
        cs[i].label=N[i]; cs[i].bytes=n; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("RtlInitStringEx (wia inline AVX2 strlen vs ntdll call+strlen)", cs, K, 200);
}
