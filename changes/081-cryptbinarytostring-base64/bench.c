// changes/081-cryptbinarytostring-base64/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern int wia_b2s(const unsigned char*, unsigned long, unsigned long, char*, unsigned long*);
typedef int (WINAPI *fn)(const unsigned char*,unsigned long,unsigned long,char*,unsigned long*);
static fn sys;
typedef struct { unsigned char* bin; unsigned long n; char* out; unsigned long cap; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; unsigned long cch=m->cap; wia_b2s(m->bin,m->n,0x1u|0x40000000u,m->out,&cch); return cch; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; unsigned long cch=m->cap; sys(m->bin,m->n,0x1u|0x40000000u,m->out,&cch); return cch; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"crypt32.dll"); sys=(fn)GetProcAddress(h,"CryptBinaryToStringA");
    static const int L[]={16,64,256,1024,4096,32768};
    static const char* N[]={"16","64","256","1024","4096","32768"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ unsigned char* b=malloc(L[i]); for(int k=0;k<L[i];k++)b[k]=(unsigned char)(k*131+7);
        cx[i].bin=b; cx[i].n=L[i]; cx[i].cap=L[i]*2+64; cx[i].out=malloc(cx[i].cap);
        cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("CryptBinaryToStringA base64  (wia SSSE3 vs crypt32)", cs, K, 150);
}
