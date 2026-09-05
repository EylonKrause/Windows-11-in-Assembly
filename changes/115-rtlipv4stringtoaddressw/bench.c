// changes/115-rtlipv4stringtoaddressw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern long wia_ipv4w(const WCHAR*, unsigned char, const WCHAR**, unsigned char*);
typedef long (WINAPI *fn)(const WCHAR*, unsigned char, const WCHAR**, unsigned char*);
static fn sys;
typedef struct { const WCHAR* s; unsigned char strict; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; const WCHAR* t; unsigned char a[4]; wia_ipv4w(m->s,m->strict,&t,a); return a[0]^a[3]; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; const WCHAR* t; unsigned char a[4]; sys(m->s,m->strict,&t,a); return a[0]^a[3]; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv4StringToAddressW");
    static ctx_t C[]={ {L"1.2.3.4",1}, {L"192.168.1.100",1}, {L"255.255.255.255",0}, {L"127.1",0}, {L"0x7f.0.0.1",0}, {L"10.0.0.255",1} };
    static const char* N[]={ "1.2.3.4","192.168.1.100","255x4","127.1(short)","hex","10.0.0.255" };
    enum{K=6}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=(size_t)wcslen(C[i].s); cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ntdll RtlIpv4StringToAddressW (wia scalar vs ntdll)", cs, K, 300);
}
