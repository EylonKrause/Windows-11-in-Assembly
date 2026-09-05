// changes/122-rtlipv6stringtoaddressex/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern long wia_ip6exa(const char*, unsigned char*, unsigned long*, unsigned short*);
typedef long (WINAPI *fn)(const char*, unsigned char*, unsigned long*, unsigned short*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ unsigned char a[16]; unsigned long sc; unsigned short p; wia_ip6exa((const char*)c,a,&sc,&p); return a[0]^a[15]^sc^p; }
static uint64_t op_sys (void*c){ unsigned char a[16]; unsigned long sc; unsigned short p; sys((const char*)c,a,&sc,&p); return a[0]^a[15]^sc^p; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv6StringToAddressExA");
    static const char* S[]={ "[2001:db8:1234:5678:9abc:def0:1234:5678]:443", "[2001:db8::1%12]:8080", "[::1]:80", "[::ffff:1.2.3.4]:53" };
    static const char* N[]={ "full+port", "compressed+scope+port", "loopback+port", "v4mapped+port" };
    enum{K=4}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=16; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=(void*)S[i]; }
    return wia_bench_compare("ntdll RtlIpv6StringToAddressExA (wia scalar vs ntdll)", cs, K, 300);
}
