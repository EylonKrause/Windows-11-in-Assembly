#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern char* wia_v6fmt(const void*, char*);
char* ref_v6fmt(const unsigned char*, char*);
void wia_v6tables_init(void);
typedef char* (WINAPI *fn)(const void*, char*);
static fn sys;
typedef struct { unsigned char a[16]; char buf[64]; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)wia_v6fmt(m->a,m->buf); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)sys(m->a,m->buf); }
#pragma optimize("", on)
int main(void){ wia_v6tables_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv6AddressToStringA");
    static ctx_t cx={{0x20,0x01,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,1}};   // 2001:db8::1
    static wia_case cs[1]={{"2001:db8::1",0,op_ours,op_sys,&cx}};
    return wia_bench_compare("RtlIpv6AddressToStringA  (wia state machine vs ntdll)", cs, 1, 400);
}
