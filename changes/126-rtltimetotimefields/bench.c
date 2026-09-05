// changes/126-rtltimetotimefields/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
typedef struct { short Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday; } WIA_TF;
extern void wia_time2fields(const long long*, WIA_TF*);
typedef void (WINAPI *fn)(long long*, WIA_TF*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ WIA_TF f; wia_time2fields((const long long*)c,&f); return (uint64_t)f.Year^f.Day^f.Milliseconds^f.Weekday; }
static uint64_t op_sys (void*c){ WIA_TF f; sys((long long*)c,&f); return (uint64_t)f.Year^f.Day^f.Milliseconds^f.Weekday; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlTimeToTimeFields");
    static long long T[]={ 133200000000000000LL, 116444736000000000LL, 0LL, 0x7FFFFFFFFFFFFFFFLL };
    static const char* N[]={"2023","1970","epoch1601","max"};
    enum{K=4}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=8; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&T[i]; }
    return wia_bench_compare("ntdll RtlTimeToTimeFields (wia mulx vs ntdll)", cs, K, 300);
}
