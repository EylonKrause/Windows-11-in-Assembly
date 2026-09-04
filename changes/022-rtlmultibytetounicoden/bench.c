#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef LONG NTSTATUS;
extern NTSTATUS wia_mb2u(wchar_t*, ULONG, PULONG, const char*, ULONG);
void wia_a2umap_init(void);
typedef NTSTATUS (WINAPI *fn)(wchar_t*, ULONG, PULONG, const char*, ULONG);
static fn sys;
typedef struct { const char* src; ULONG sb; wchar_t* dst; ULONG mx; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; ULONG o=0; wia_mb2u(m->dst,m->mx,&o,m->src,m->sb); return o; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; ULONG o=0; sys(m->dst,m->mx,&o,m->src,m->sb); return o; }
int main(void){
    wia_a2umap_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlMultiByteToUnicodeN");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ char*s=malloc(L[i]+1); for(int k=0;k<L[i];k++)s[k]=(char)(0x61+(k&15));
        cx[i].src=s; cx[i].sb=(ULONG)L[i]; cx[i].dst=malloc((L[i]+8)*2); cx[i].mx=(ULONG)((L[i]+8)*2);
        cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("RtlMultiByteToUnicodeN  (wia AVX2 vs ntdll)", cs, K, 200);
}
