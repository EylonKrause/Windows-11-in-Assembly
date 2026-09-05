// changes/072-ultow/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include "bench.h"
extern wchar_t* wia_ultow(unsigned long, wchar_t*, int);
wchar_t* ref_ultow(unsigned long, wchar_t*, int);
void wia_dec2_init(void);
typedef wchar_t* (__cdecl *fn)(unsigned long, wchar_t*, int);
static fn sys;
typedef struct { unsigned long v; int radix; wchar_t buf[48]; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)wia_ultow(m->v,m->buf,m->radix)[0]; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)sys(m->v,m->buf,m->radix)[0]; }
#pragma optimize("", on)
int main(void){
    wia_dec2_init();
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"_ultow");
    static const unsigned long V[]={5,12345,1234567890,4294967295UL,4294967295UL};
    static const int R[]={10,10,10,10,16};
    static const char* N[]={"r10:1dig","r10:5dig","r10:10dig","r10:max","r16:8dig"};
    enum{K=5}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        cx[i].v=V[i]; cx[i].radix=R[i];
        cs[i].label=N[i]; cs[i].bytes=0; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("_ultow  (wia 2-digit-table / nibble vs ucrtbase)", cs, K, 300);
}
