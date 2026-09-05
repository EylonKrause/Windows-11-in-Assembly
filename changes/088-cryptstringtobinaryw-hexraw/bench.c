// changes/088-cryptstringtobinaryw-hexraw/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include "bench.h"
extern int wia_s2bhw(const wchar_t*, unsigned long, unsigned long, unsigned char*, unsigned long*, unsigned long*, unsigned long*);
void wia_hexrev_init(void);
typedef int (WINAPI *decf)(const wchar_t*,unsigned long,unsigned long,unsigned char*,unsigned long*,unsigned long*,unsigned long*);
typedef int (WINAPI *encf)(const unsigned char*,unsigned long,unsigned long,wchar_t*,unsigned long*);
static decf sys;
typedef struct { wchar_t* hx; unsigned long slen; unsigned char* out; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; unsigned long cb=49152,sk,fl; wia_s2bhw(m->hx,m->slen,0x0cu,m->out,&cb,&sk,&fl); return cb; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; unsigned long cb=49152,sk,fl; sys(m->hx,m->slen,0x0cu,m->out,&cb,&sk,&fl); return cb; }
#pragma optimize("", on)
int main(void){
    wia_hexrev_init();
    HMODULE h=LoadLibraryW(L"crypt32.dll"); sys=(decf)GetProcAddress(h,"CryptStringToBinaryW");
    encf enc=(encf)GetProcAddress(h,"CryptBinaryToStringW");
    static const int L[]={16,64,256,1024,4096,32768};
    static const char* N[]={"16","64","256","1024","4096","32768"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ unsigned char* b=malloc(L[i]); for(int k=0;k<L[i];k++)b[k]=(unsigned char)(k*131+7);
        wchar_t* s=malloc((L[i]*2+64)*2); unsigned long cch=L[i]*2+64; enc(b,L[i],0x0cu|0x40000000u,s,&cch);
        cx[i].hx=s; cx[i].slen=cch; cx[i].out=malloc(L[i]+64);
        cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("CryptStringToBinaryW HEXRAW decode  (wia SSSE3 vs crypt32)", cs, K, 150);
}
