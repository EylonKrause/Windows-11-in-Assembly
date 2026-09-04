#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
extern char* wia_itoa(int, char*, int);
char* ref_itoa(int, char*, int);
void wia_dec2b_init(void);
typedef char* (__cdecl *fn)(int, char*, int);
static fn sys;
typedef struct { int v; int radix; char buf[48]; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned char)wia_itoa(m->v,m->buf,m->radix)[0]; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned char)sys(m->v,m->buf,m->radix)[0]; }
#pragma optimize("", on)
int main(void){
    wia_dec2b_init();
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"_itoa");
    static const int V[]={-5,-12345,-1234567890,2147483647,-1};
    static const int R[]={10,10,10,10,16};
    static const char* N[]={"neg1d","neg5d","neg10d","posmax","r16"};
    enum{K=5}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ cx[i].v=V[i]; cx[i].radix=R[i];
        cs[i].label=N[i]; cs[i].bytes=0; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("_itoa  (wia 2-digit-table / nibble vs ucrtbase, signed)", cs, K, 300);
}
