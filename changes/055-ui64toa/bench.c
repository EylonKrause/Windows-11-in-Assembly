#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern char* wia_ui64toa(unsigned long long, char*, int);
char* ref_ui64toa(unsigned long long, char*, int);
void wia_dec2b_init(void);
typedef char* (__cdecl *fn)(unsigned long long, char*, int);
static fn sys;
typedef struct { unsigned long long v; int radix; char buf[80]; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned char)wia_ui64toa(m->v,m->buf,m->radix)[0]; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned char)sys(m->v,m->buf,m->radix)[0]; }
#pragma optimize("", on)
int main(void){
    wia_dec2b_init();
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"_ui64toa");
    static const unsigned long long V[]={99ULL,123456ULL,12345678901234ULL,18446744073709551615ULL,18446744073709551615ULL};
    static const int R[]={10,10,10,10,16};
    static const char* N[]={"r10:2d","r10:6d","r10:14d","r10:20d","r16:16d"};
    enum{K=5}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ cx[i].v=V[i]; cx[i].radix=R[i];
        cs[i].label=N[i]; cs[i].bytes=0; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("_ui64toa  (wia 2-digit-table / nibble vs ucrtbase)", cs, K, 300);
}
