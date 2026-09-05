// changes/119-rtlethernetstringtoaddress/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern long wia_ethstra(const char*, const char**, unsigned char*);
typedef long (WINAPI *fn)(const char*, const char**, unsigned char*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ const char* t; unsigned char a[6]; wia_ethstra((const char*)c,&t,a); return a[0]^a[5]; }
static uint64_t op_sys (void*c){ const char* t; unsigned char a[6]; sys((const char*)c,&t,a); return a[0]^a[5]; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlEthernetStringToAddressA");
    static const char* S[]={ "01-23-45-67-89-ab", "01:23:45:67:89:AB" };
    static const char* N[]={ "dash", "colon" };
    enum{K=2}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=17; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=(void*)S[i]; }
    return wia_bench_compare("ntdll RtlEthernetStringToAddressA (wia scalar vs ntdll)", cs, K, 400);
}
