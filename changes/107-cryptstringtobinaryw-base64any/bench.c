// changes/107-cryptstringtobinaryw-base64any/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern void wia_b64rev_init(void);
extern int wia_s2bw_any(const WCHAR*, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
typedef BOOL (WINAPI *fn)(LPCWSTR,DWORD,DWORD,BYTE*,DWORD*,DWORD*,DWORD*);
static fn sys;
typedef struct { const WCHAR* s; BYTE* out; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; DWORD cb=1u<<30,sk=0,ff=0; wia_s2bw_any(m->s,0,0x6,m->out,&cb,&sk,&ff); return cb; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; DWORD cb=1u<<30,sk=0,ff=0; sys(m->s,0,0x6,m->out,&cb,&sk,&ff); return cb; }
#pragma optimize("", on)
int main(void){
    wia_b64rev_init(); HMODULE h=LoadLibraryW(L"crypt32.dll"); sys=(fn)GetProcAddress(h,"CryptStringToBinaryW");
    static const int L[]={16,64,256,1024,8192,49152};
    static const char* N[]={"16","64","256","1024","8192","49152"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        int n=L[i]; BYTE* d=(BYTE*)malloc(n); for(int k=0;k<n;k++) d[k]=(BYTE)(k*7+3);
        DWORD c=0; CryptBinaryToStringW(d,n,0x1,NULL,&c); WCHAR* b=(WCHAR*)malloc(c*2); DWORD c2=c;
        CryptBinaryToStringW(d,n,0x1,b,&c2);
        cx[i].s=b; cx[i].out=(BYTE*)malloc(n+16);
        cs[i].label=N[i]; cs[i].bytes=n; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("CryptStringToBinaryW BASE64_ANY decode (plain base64, wia scalar vs crypt32)", cs, K, 200);
}
