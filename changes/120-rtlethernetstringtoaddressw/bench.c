// changes/120-rtlethernetstringtoaddressw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern long wia_ethstrw(const WCHAR*, const WCHAR**, unsigned char*);
typedef long (WINAPI *fn)(const WCHAR*, const WCHAR**, unsigned char*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ const WCHAR* t; unsigned char a[6]; wia_ethstrw((const WCHAR*)c,&t,a); return a[0]^a[5]; }
static uint64_t op_sys (void*c){ const WCHAR* t; unsigned char a[6]; sys((const WCHAR*)c,&t,a); return a[0]^a[5]; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlEthernetStringToAddressW");
    static const WCHAR* S[]={ L"01-23-45-67-89-ab", L"01:23:45:67:89:AB" };
    static const char* N[]={ "dash", "colon" };
    enum{K=2}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=17; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=(void*)S[i]; }
    return wia_bench_compare("ntdll RtlEthernetStringToAddressW (wia scalar vs ntdll)", cs, K, 400);
}
