#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
extern NTSTATUS wia_guidfmt(const GUID*, U*, BOOLEAN);
NTSTATUS ref_guidfmt(const GUID*, U*);
void wia_hex2_init(void);
typedef NTSTATUS (WINAPI *fn)(const GUID*, U*, BOOLEAN);
static fn sys;
typedef struct { GUID g; wchar_t buf[64]; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; U s={0,80,m->buf}; NTSTATUS r=wia_guidfmt(&m->g,&s,FALSE); return (uint64_t)(unsigned)r+s.Length; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; U s={0,80,m->buf}; NTSTATUS r=sys(&m->g,&s,FALSE); return (uint64_t)(unsigned)r+s.Length; }
#pragma optimize("", on)
int main(void){
    wia_hex2_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlStringFromGUIDEx");
    static ctx_t cx; static wia_case cs[1];
    unsigned char* p=(unsigned char*)&cx.g; for(int i=0;i<16;i++) p[i]=(unsigned char)(i*17+3);
    cs[0].label="guid"; cs[0].bytes=0; cs[0].ours=op_ours; cs[0].system=op_sys; cs[0].ctx=&cx;
    return wia_bench_compare("RtlStringFromGUIDEx  (wia table byte->hex vs ntdll)", cs, 1, 400);
}
