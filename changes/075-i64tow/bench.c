// changes/075-i64tow/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include "bench.h"
extern wchar_t* wia_i64tow(long long, wchar_t*, int);
wchar_t* ref_i64tow(long long, wchar_t*, int);
void wia_dec2_init(void);
typedef wchar_t* (__cdecl *fn)(long long, wchar_t*, int);
static fn sys;
typedef struct { long long v; int radix; wchar_t buf[80]; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)wia_i64tow(m->v,m->buf,m->radix)[0]; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)sys(m->v,m->buf,m->radix)[0]; }
#pragma optimize("", on)
int main(void){
    wia_dec2_init();
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"_i64tow");
    static const long long V[]={5LL,-12345LL,12345678901234LL,(long long)0x8000000000000000ULL,-1LL};
    static const int R[]={10,10,10,10,16};
    static const char* N[]={"r10:1dig","r10:neg5","r10:14dig","r10:i64min","r16:16f"};
    enum{K=5}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        cx[i].v=V[i]; cx[i].radix=R[i];
        cs[i].label=N[i]; cs[i].bytes=0; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("_i64tow  (wia 2-digit-table / nibble vs ucrtbase)", cs, K, 300);
}
