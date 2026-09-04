#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef LONG NTSTATUS;
extern NTSTATUS wia_rtlhash(const USTR*, unsigned char, unsigned long, unsigned long*);
void wia_upcase_init(void);
typedef NTSTATUS (WINAPI *fn)(const USTR*,BOOLEAN,ULONG,PULONG);
static fn sys;
typedef struct { USTR u; unsigned char ci; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; unsigned long h=0; wia_rtlhash(&m->u,m->ci,0,&h); return h; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; ULONG h=0; sys(&m->u,(BOOLEAN)m->ci,0,&h); return h; }
static wchar_t* mk(int n){ wchar_t* s=malloc((n+1)*2); for(int i=0;i<n;i++)s[i]=L'a'+(wchar_t)(i&15); s[n]=0; return s; }
int main(void){
    wia_upcase_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlHashUnicodeString");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* Ncs[]={"8/cs","32/cs","128/cs","512/cs","4096/cs","32000/cs"};
    static const char* Nci[]={"8/CI","32/CI","128/CI","512/CI","4096/CI","32000/CI"};
    enum{K=6}; static ctx_t cx[2*K]; static wia_case cs[2*K];
    for(int i=0;i<K;++i){ wchar_t*a=mk(L[i]);
        for(int m=0;m<2;m++){ ctx_t*c=&cx[m*K+i]; c->u.Length=c->u.MaximumLength=(unsigned short)(L[i]*2); c->u.Buffer=a; c->ci=(unsigned char)m;
            wia_case*w=&cs[m*K+i]; w->label=(m?Nci:Ncs)[i]; w->bytes=L[i]*2; w->ours=op_ours; w->system=op_sys; w->ctx=c; } }
    return wia_bench_compare("RtlHashUnicodeString  (wia AVX2 vs ntdll; cs + CI)", cs, 2*K, 200);
}
