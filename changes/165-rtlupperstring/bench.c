// changes/165-rtlupperstring/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
typedef struct { USHORT Length, MaximumLength; PCHAR Buffer; } WIA_STRING;
extern void wia_rtlupperstring(WIA_STRING*, const WIA_STRING*);
typedef void (NTAPI *fn)(WIA_STRING*, const WIA_STRING*);
static fn sys;
typedef struct { WIA_STRING d, s; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; wia_rtlupperstring(&k->d,&k->s); return k->d.Length; }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; sys(&k->d,&k->s); return k->d.Length; }
#pragma optimize("", on)
static char src[8192], dst[8192];
int main(void){
    HMODULE n=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(n,"RtlUpperString");
    for(int i=0;i<8192;i++) src[i]=(char)('a'+(i%26));
    static const int L[]={8,16,64,254,1024,4096};
    static const char* N[]={"8 bytes","16 bytes","64 bytes","254 bytes","1024 bytes","4096 bytes"};
    static CASE C[6]; static wia_case cs[6];
    for(int i=0;i<6;++i){
        C[i].s.Length=(USHORT)L[i]; C[i].s.MaximumLength=(USHORT)L[i]; C[i].s.Buffer=src;
        C[i].d.Length=0; C[i].d.MaximumLength=8000; C[i].d.Buffer=dst;
        cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    return wia_bench_compare("ntdll RtlUpperString (wia AVX2 vs ntdll)", cs, 6, 300);
}
