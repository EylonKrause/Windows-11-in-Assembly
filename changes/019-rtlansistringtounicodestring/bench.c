#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ASTR;
typedef LONG NTSTATUS;
extern NTSTATUS wia_a2u(USTR*, const ASTR*, unsigned char);
void wia_a2umap_init(void);
typedef NTSTATUS (WINAPI *fn)(USTR*,const ASTR*,BOOLEAN);
static fn sys;
typedef struct { ASTR src; USTR dst; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; m->dst.Length=0; return (uint64_t)(unsigned)wia_a2u(&m->dst,&m->src,0); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; m->dst.Length=0; return (uint64_t)(unsigned)sys(&m->dst,&m->src,FALSE); }
int main(void){
    wia_a2umap_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlAnsiStringToUnicodeString");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ char*s=malloc(L[i]+1); wchar_t*d=malloc((L[i]+8)*2); for(int k=0;k<L[i];k++)s[k]=(char)('a'+(k&15));
        cx[i].src.Length=cx[i].src.MaximumLength=(unsigned short)L[i]; cx[i].src.Buffer=s;
        cx[i].dst.Length=0; cx[i].dst.MaximumLength=(unsigned short)((L[i]+8)*2); cx[i].dst.Buffer=d;
        cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("RtlAnsiStringToUnicodeString  (wia AVX2 vs ntdll, no-alloc)", cs, K, 200);
}
