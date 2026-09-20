// changes/006-crc32/bench.c: wia_crc32 (PCLMUL) vs live ntdll!RtlComputeCrc32.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern uint32_t wia_crc32(uint32_t,const void*,int);
typedef DWORD (WINAPI *rtl_fn)(DWORD,const void*,INT);
static rtl_fn sysf;
typedef struct{ const void* p; int n; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return wia_crc32(0,m->p,m->n); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return sysf(0,m->p,m->n); }
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sysf=(rtl_fn)GetProcAddress(h,"RtlComputeCrc32");
    static const int L[]={16,64,256,1024,8192,65536,1048576};
    static const char* N[]={"16","64","256","1KB","8KB","64KB","1MB"};
    enum{K=7}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ unsigned char*b=(unsigned char*)malloc(L[i]); for(int k=0;k<L[i];k++)b[k]=(unsigned char)(k*97+13); cx[i].p=b;cx[i].n=L[i]; cs[i].label=N[i];cs[i].bytes=L[i];cs[i].ours=op_ours;cs[i].system=op_sys;cs[i].ctx=&cx[i]; }
    return wia_bench_compare("crc32  (wia VPCLMULQDQ vs ntdll RtlComputeCrc32)", cs, K, 200);
}
