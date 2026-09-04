#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern wchar_t* wia_ip4fmtw(const void*, wchar_t*);
unsigned short* ref_ip4fmtw(const unsigned char*, unsigned short*);
void wia_dec2_init(void);
typedef wchar_t* (WINAPI *fn)(const void*, wchar_t*);
static fn sys;
typedef struct { unsigned char a[4]; wchar_t buf[24]; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)wia_ip4fmtw(m->a,m->buf); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)sys(m->a,m->buf); }
#pragma optimize("", on)
int main(void){ wia_dec2_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv4AddressToStringW");
    static ctx_t cx={{192,168,100,201}}; static wia_case cs[1]={{"192.168.100.201",0,op_ours,op_sys,&cx}};
    return wia_bench_compare("RtlIpv4AddressToStringW  (wia table vs ntdll)", cs, 1, 400);
}
