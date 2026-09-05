// changes/128-rtlsecondssince1970totime/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern void wia_secs2time(unsigned long, long long*);
typedef void (WINAPI *fn)(unsigned long, long long*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ long long t; wia_secs2time(*(unsigned long*)c,&t); return (uint64_t)t; }
static uint64_t op_sys (void*c){ long long t; sys(*(unsigned long*)c,&t); return (uint64_t)t; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlSecondsSince1970ToTime");
    static unsigned long S[]={0ul,1700000000ul,2147483648ul,4294967295ul};
    static const char* N[]={"epoch","2023","2p31","max"};
    enum{K=4}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=8; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&S[i]; }
    return wia_bench_compare("ntdll RtlSecondsSince1970ToTime (wia vs ntdll)", cs, K, 300);
}
