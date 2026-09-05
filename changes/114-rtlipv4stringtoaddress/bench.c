// changes/114-rtlipv4stringtoaddress/bench.c
// wia_ipv4a vs live ntdll!RtlIpv4StringToAddressA across representative inputs.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern long wia_ipv4a(const char*, unsigned char, const char**, unsigned char*);
typedef long (WINAPI *fn)(const char*, unsigned char, const char**, unsigned char*);
static fn sys;
typedef struct { const char* s; unsigned char strict; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; const char* t; unsigned char a[4]; wia_ipv4a(m->s,m->strict,&t,a); return a[0]^a[3]; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; const char* t; unsigned char a[4]; sys(m->s,m->strict,&t,a); return a[0]^a[3]; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv4StringToAddressA");
    static ctx_t C[]={ {"1.2.3.4",1}, {"192.168.1.100",1}, {"255.255.255.255",0}, {"127.1",0}, {"0x7f.0.0.1",0}, {"10.0.0.255",1} };
    static const char* N[]={ "1.2.3.4", "192.168.1.100", "255x4", "127.1(short)", "hex", "10.0.0.255" };
    enum{K=6}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=(size_t)strlen(C[i].s); cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ntdll RtlIpv4StringToAddressA (wia scalar vs ntdll)", cs, K, 300);
}
