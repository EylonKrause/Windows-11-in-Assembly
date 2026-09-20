#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } US;
extern LONG wia_appendss(US*, US*);
typedef LONG (NTAPI *fn)(US*, US*);
static fn sys;
typedef struct { wchar_t* buf; USHORT maxlen; US src; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; US u={0,m->maxlen,m->buf}; wia_appendss(&u,&m->src); return u.Length; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; US u={0,m->maxlen,m->buf}; sys(&u,&m->src); return u.Length; }
#pragma optimize("", on)
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlAppendUnicodeStringToString");
    static const int L[]={2,4,8,16,32,128};
    static const char* N[]={"2","4","8","16","32","128"};
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        int n=L[i];
        wchar_t* add=(wchar_t*)malloc((n+1)*2);
        for(int k=0;k<n;k++) add[k]=(wchar_t)(L'a'+(k%26));
        cx[i].buf=(wchar_t*)malloc((n+8)*2); cx[i].maxlen=(USHORT)((n+4)*2);
        cx[i].src.Length=(USHORT)(n*2); cx[i].src.MaximumLength=(USHORT)(n*2); cx[i].src.Buffer=add;
        cs[i].label=N[i]; cs[i].bytes=n*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("RtlAppendUnicodeStringToString (wia frameless SSE copy vs ntdll call)", cs, K, 200);
}
