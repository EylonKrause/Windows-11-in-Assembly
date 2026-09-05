// changes/117-rtlipv4stringtoaddressexw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern long wia_ipv4exw(const WCHAR*, unsigned char, unsigned char*, unsigned short*);
typedef long (WINAPI *fn)(const WCHAR*, unsigned char, unsigned char*, unsigned short*);
static fn sys;
typedef struct { const WCHAR* s; unsigned char strict; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; unsigned char a[4]; unsigned short p; wia_ipv4exw(m->s,m->strict,a,&p); return a[0]^p; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; unsigned char a[4]; unsigned short p; sys(m->s,m->strict,a,&p); return a[0]^p; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv4StringToAddressExW");
    static ctx_t C[]={ {L"1.2.3.4:80",1}, {L"192.168.1.100:65535",1}, {L"10.0.0.1",1}, {L"127.1:8080",0}, {L"0x7f.0.0.1:22",0}, {L"255.255.255.255:1",0} };
    static const char* N[]={ "addr:80","addr:65535","addr-noport","short:8080","hex:22","255x4:1" };
    enum{K=6}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=(size_t)wcslen(C[i].s); cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ntdll RtlIpv4StringToAddressExW (wia scalar vs ntdll)", cs, K, 300);
}
