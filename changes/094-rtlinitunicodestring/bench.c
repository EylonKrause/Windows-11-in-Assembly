// changes/094-rtlinitunicodestring/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { USHORT Length; USHORT MaximumLength; PWSTR Buffer; } UNICODE_STRING, *PUNICODE_STRING;
extern void wia_rtlinitus(PUNICODE_STRING, const wchar_t*);
typedef VOID (NTAPI *fn)(PUNICODE_STRING, PCWSTR);
static fn sys;
typedef struct { const wchar_t* s; UNICODE_STRING us; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; wia_rtlinitus(&m->us,m->s); return m->us.Length; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; sys(&m->us,m->s); return m->us.Length; }
#pragma optimize("", on)
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlInitUnicodeString");
    // realistic name/path lengths (RtlInitUnicodeString is dominated by short strings)
    static const int L[]={4,8,16,32,64,260};
    static const char* N[]={"4","8","16","32","64","260"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        int n=L[i];
        wchar_t* s=(wchar_t*)malloc((n+1)*2);
        for(int k=0;k<n;k++) s[k]=(wchar_t)(L'a'+(k%26));
        s[n]=0;
        cx[i].s=s;
        cs[i].label=N[i]; cs[i].bytes=n*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("RtlInitUnicodeString (wia inline AVX2 wcslen vs ntdll call+wcslen)", cs, K, 200);
}
