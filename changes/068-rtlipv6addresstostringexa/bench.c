#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
typedef LONG NTSTATUS;
extern NTSTATUS wia_v6ex(const void*, ULONG, USHORT, char*, ULONG*);
NTSTATUS ref_v6ex(const unsigned char*, unsigned long, unsigned short, char*, unsigned long*);
void wia_v6tables_init(void);
typedef NTSTATUS (WINAPI *fn)(const void*, ULONG, USHORT, char*, ULONG*);
static fn sys;
typedef struct { unsigned char a[16]; ULONG scope; USHORT port; char buf[96]; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; ULONG s=96; return (uint64_t)(unsigned)wia_v6ex(m->a,m->scope,m->port,m->buf,&s)+s; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; ULONG s=96; return (uint64_t)(unsigned)sys(m->a,m->scope,m->port,m->buf,&s)+s; }
#pragma optimize("", on)
int main(void){ wia_v6tables_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv6AddressToStringExA");
    static ctx_t cx={{0x20,0x01,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,1},5,0x5000};
    static wia_case cs[1]={{"[2001:db8::1%5]:80",0,op_ours,op_sys,&cx}};
    return wia_bench_compare("RtlIpv6AddressToStringExA  (wia vs ntdll)", cs, 1, 400);
}
