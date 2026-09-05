// changes/130-rtlsetbits/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern void wia_setbits(RTL_BITMAP*, unsigned long, unsigned long);
typedef void (WINAPI *fn)(RTL_BITMAP*, ULONG, ULONG);
static fn sys;
typedef struct { RTL_BITMAP* bm; unsigned long start, num; } CASE;
static uint64_t op_ours(void*c){ CASE*k=(CASE*)c; wia_setbits(k->bm,k->start,k->num); return k->bm->Buffer[0]; }
static uint64_t op_sys (void*c){ CASE*k=(CASE*)c; sys(k->bm,k->start,k->num); return k->bm->Buffer[0]; }
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlSetBits");
    enum{K=6}; static RTL_BITMAP bm; static CASE ca[K]; static wia_case cs[K];
    static const unsigned long ST[] ={ 3,  0,   3,    0,     3,     0};
    static const unsigned long NU[] ={ 1, 64, 200, 4096, 40000, 262144};
    static const char* N[]={"1bit","64","200","4096","40000","262144"};
    unsigned long* buf=malloc(40000*4);
    bm.SizeOfBitMap=1000000; bm.Buffer=buf;
    for(int i=0;i<K;++i){ ca[i].bm=&bm; ca[i].start=ST[i]; ca[i].num=NU[i];
        cs[i].label=N[i]; cs[i].bytes=NU[i]/8+1; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&ca[i]; }
    return wia_bench_compare("ntdll RtlSetBits (wia AVX2 vs ntdll)", cs, K, 200);
}
