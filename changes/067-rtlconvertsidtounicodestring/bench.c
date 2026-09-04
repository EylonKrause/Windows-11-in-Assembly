#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
extern NTSTATUS wia_sidfmt(U*, void*, BOOLEAN);
NTSTATUS ref_sidfmt(U*, const unsigned char*);
typedef NTSTATUS (WINAPI *fn)(U*, void*, BOOLEAN);
static fn sys;
typedef struct { unsigned char sid[28]; wchar_t buf[64]; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; U s={0,128,m->buf}; NTSTATUS r=wia_sidfmt(&s,m->sid,FALSE); return (uint64_t)(unsigned)r+s.Length; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; U s={0,128,m->buf}; NTSTATUS r=sys(&s,m->sid,FALSE); return (uint64_t)(unsigned)r+s.Length; }
#pragma optimize("", on)
void wia_dec2b_init(void);
int main(void){
    wia_dec2b_init(); HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlConvertSidToUnicodeString");
    static ctx_t cx; cx.sid[0]=1; cx.sid[1]=5; memset(cx.sid+2,0,6); cx.sid[7]=5;
    unsigned sa[5]={21,1111111111u,2222222222u,3333333333u,500}; memcpy(cx.sid+8,sa,20);
    static wia_case cs[1]={{"S-1-5-21-x-x-x-500",0,op_ours,op_sys,&cx}};
    return wia_bench_compare("RtlConvertSidToUnicodeString  (wia state machine vs ntdll)", cs, 1, 400);
}
