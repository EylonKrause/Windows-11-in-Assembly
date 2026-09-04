// changes/052-rtlintegertounicodestring/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
extern NTSTATUS wia_itos(ULONG, ULONG, U*);
NTSTATUS ref_itos(unsigned long, unsigned long, U*);   // pulled in only to satisfy the shared link
void wia_dec2_init(void);
typedef NTSTATUS (WINAPI *fn)(ULONG, ULONG, U*);
static fn sys;
typedef struct { ULONG v; ULONG base; wchar_t buf[48]; U s; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; m->s.MaximumLength=96; m->s.Buffer=m->buf; return (uint64_t)(unsigned)wia_itos(m->v,m->base,&m->s)+m->s.Length; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; m->s.MaximumLength=96; m->s.Buffer=m->buf; return (uint64_t)(unsigned)sys(m->v,m->base,&m->s)+m->s.Length; }
#pragma optimize("", on)
int main(void){
    wia_dec2_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIntegerToUnicodeString");
    static const ULONG V[]={5, 99, 12345, 1000000, 1000000000u, 4294967295u};
    static const char* N[]={"1digit","2digit","5digit","7digit","10digit","10dig(max)"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        cx[i].v=V[i]; cx[i].base=10;
        cs[i].label=N[i]; cs[i].bytes=0; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("RtlIntegerToUnicodeString  (wia 2-digit-table vs ntdll, base 10)", cs, K, 300);
}
