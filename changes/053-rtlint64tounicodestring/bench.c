// changes/053-rtlint64tounicodestring/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
extern NTSTATUS wia_itos64(ULONGLONG, ULONG, U*);
NTSTATUS ref_itos64(unsigned long long, unsigned long, U*);
void wia_dec2_init(void);
typedef NTSTATUS (WINAPI *fn)(ULONGLONG, ULONG, U*);
static fn sys;
typedef struct { ULONGLONG v; ULONG base; wchar_t buf[80]; U s; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; m->s.MaximumLength=160; m->s.Buffer=m->buf; return (uint64_t)(unsigned)wia_itos64(m->v,m->base,&m->s)+m->s.Length; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; m->s.MaximumLength=160; m->s.Buffer=m->buf; return (uint64_t)(unsigned)sys(m->v,m->base,&m->s)+m->s.Length; }
#pragma optimize("", on)
int main(void){
    wia_dec2_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlInt64ToUnicodeString");
    static const ULONGLONG V[]={99ULL, 123456ULL, 12345678901234ULL, 1000000000000000000ULL, 18446744073709551615ULL};
    static const char* N[]={"2digit","6digit","14digit","19digit","20dig(max)"};
    enum{K=5}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        cx[i].v=V[i]; cx[i].base=10;
        cs[i].label=N[i]; cs[i].bytes=0; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("RtlInt64ToUnicodeString  (wia 2-digit-table vs ntdll, base 10)", cs, K, 300);
}
