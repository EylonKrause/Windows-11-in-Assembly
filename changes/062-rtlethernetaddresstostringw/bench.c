#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern wchar_t* wia_macfmtw(const void*, wchar_t*);
unsigned short* ref_macfmtw(const unsigned char*, unsigned short*);
void wia_hex2uw_init(void);
typedef wchar_t* (WINAPI *fn)(const void*, wchar_t*);
static fn sys;
typedef struct { unsigned char a[6]; wchar_t buf[24]; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)wia_macfmtw(m->a,m->buf); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)sys(m->a,m->buf); }
#pragma optimize("", on)
int main(void){ wia_hex2uw_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlEthernetAddressToStringW");
    static ctx_t cx={{0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}}; static wia_case cs[1]={{"mac",0,op_ours,op_sys,&cx}};
    return wia_bench_compare("RtlEthernetAddressToStringW  (wia table vs ntdll)", cs, 1, 400);
}
