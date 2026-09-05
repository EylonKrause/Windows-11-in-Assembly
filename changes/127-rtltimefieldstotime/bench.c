// changes/127-rtltimefieldstotime/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
typedef struct { short Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday; } WIA_TF;
extern unsigned char wia_fields2time(const WIA_TF*, long long*);
typedef unsigned char (WINAPI *fn)(WIA_TF*, long long*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ long long t=0; unsigned char r=wia_fields2time((const WIA_TF*)c,&t); return (uint64_t)t^r; }
static uint64_t op_sys (void*c){ long long t=0; unsigned char r=sys((WIA_TF*)c,&t); return (uint64_t)t^r; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlTimeFieldsToTime");
    static WIA_TF F[4]={ {2023,6,15,13,45,7,123,0}, {1970,1,1,0,0,0,0,0}, {2024,2,29,23,59,59,999,0}, {30827,12,31,23,59,59,999,0} };
    static const char* N[]={"2023","1970","leapday","max"};
    enum{K=4}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=16; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&F[i]; }
    return wia_bench_compare("ntdll RtlTimeFieldsToTime (wia vs ntdll)", cs, K, 300);
}
