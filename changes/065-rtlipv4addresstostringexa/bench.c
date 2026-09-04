#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
typedef LONG NTSTATUS;
extern NTSTATUS wia_ip4ex(const void*, USHORT, char*, ULONG*);
NTSTATUS ref_ip4ex(const unsigned char*, unsigned short, char*, unsigned long*);
void wia_dec2b_init(void);
typedef NTSTATUS (WINAPI *fn)(const void*, USHORT, char*, ULONG*);
static fn sys;
typedef struct { unsigned char a[4]; USHORT port; char buf[40]; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; ULONG s=40; return (uint64_t)(unsigned)wia_ip4ex(m->a,m->port,m->buf,&s)+s; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; ULONG s=40; return (uint64_t)(unsigned)sys(m->a,m->port,m->buf,&s)+s; }
#pragma optimize("", on)
int main(void){ wia_dec2b_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv4AddressToStringExA");
    static ctx_t cx={{192,168,0,1},0x5000}; static wia_case cs[1]={{"192.168.0.1:80",0,op_ours,op_sys,&cx}};
    return wia_bench_compare("RtlIpv4AddressToStringExA  (wia table vs ntdll)", cs, 1, 400);
}
