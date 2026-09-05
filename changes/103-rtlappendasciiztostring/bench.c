#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { USHORT Length, MaximumLength; char* Buffer; } AS;
extern LONG wia_appendaz(AS*, const char*);
typedef LONG (NTAPI *fn)(AS*, const char*);
static fn sys;
typedef struct { char* buf; USHORT maxlen; const char* add; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; AS u={0,m->maxlen,m->buf}; wia_appendaz(&u,m->add); return u.Length; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; AS u={0,m->maxlen,m->buf}; sys(&u,m->add); return u.Length; }
#pragma optimize("", on)
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlAppendAsciizToString");
    static const int L[]={2,4,8,16,32,128}; static const char* N[]={"2","4","8","16","32","128"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){ int n=L[i]; char* add=(char*)malloc(n+1); for(int k=0;k<n;k++)add[k]='a'+(k%26); add[n]=0;
        cx[i].buf=(char*)malloc(n+8); cx[i].maxlen=(USHORT)(n+4); cx[i].add=add;
        cs[i].label=N[i]; cs[i].bytes=n; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i]; }
    return wia_bench_compare("RtlAppendAsciizToString (wia inline AVX2 strlen+SSE copy vs ntdll call+call)", cs, K, 200);
}
