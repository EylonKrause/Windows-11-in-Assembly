// changes/092-cryptbinarytostring-base64header/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern int wia_b2sh64(const BYTE*, DWORD, DWORD, char*, DWORD*);
typedef BOOL (WINAPI *fn)(const BYTE*,DWORD,DWORD,LPSTR,DWORD*);
static fn sys;
typedef struct { const BYTE* in; DWORD n; DWORD fl; char* out; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; DWORD cch=1u<<30; wia_b2sh64(m->in,m->n,m->fl,m->out,&cch); return cch; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; DWORD cch=1u<<30; sys(m->in,m->n,m->fl,m->out,&cch); return cch; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"crypt32.dll"); sys=(fn)GetProcAddress(h,"CryptBinaryToStringA");
    static const int L[]={16,64,256,1024,8192,65536};
    static const char* N[]={"16","64","256","1024","8192","65536"};
    DWORD FL=0x0;   // BASE64HEADER (CERTIFICATE)
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        int n=L[i];
        BYTE* in=(BYTE*)malloc(n);
        for(int k=0;k<n;k++) in[k]=(BYTE)(k*7+3);
        DWORD need=0; sys(in,n,FL,NULL,&need);
        char* out=(char*)malloc(need+16);
        cx[i].in=in; cx[i].n=n; cx[i].fl=FL; cx[i].out=out;
        cs[i].label=N[i]; cs[i].bytes=n; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("CryptBinaryToStringA BASE64HEADER (wia SSSE3 vs crypt32 scalar)", cs, K, 200);
}
