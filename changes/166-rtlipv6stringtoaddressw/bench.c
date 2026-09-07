// changes/166-rtlipv6stringtoaddressw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern long wia_ip6w(const wchar_t*, const wchar_t**, unsigned char*);
typedef long (WINAPI *fn)(const wchar_t*, const wchar_t**, unsigned char*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ const wchar_t* t; unsigned char a[16]; wia_ip6w((const wchar_t*)c,&t,a); return a[0]^a[15]; }
static uint64_t op_sys (void*c){ const wchar_t* t; unsigned char a[16]; sys((const wchar_t*)c,&t,a); return a[0]^a[15]; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv6StringToAddressW");
    static const wchar_t* S[]={ L"2001:0db8:1234:5678:9abc:def0:1234:5678", L"2001:db8::1", L"::1", L"::ffff:1.2.3.4" };
    static const char* N[]={ "full", "compressed", "loopback", "v4mapped" };
    enum{K=4}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=16; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=(void*)S[i]; }
    return wia_bench_compare("ntdll RtlIpv6StringToAddressW (wia scalar vs ntdll)", cs, K, 300);
}
