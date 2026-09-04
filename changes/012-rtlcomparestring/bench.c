#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ASTR;
extern long wia_rtlcmpstr(const ASTR*, const ASTR*, unsigned char);
void wia_upcase_ansi_init(void);
typedef LONG (WINAPI *fn)(const ASTR*,const ASTR*,BOOLEAN);
static fn sys;
typedef struct { ASTR u1,u2; unsigned char ci; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)wia_rtlcmpstr(&m->u1,&m->u2,m->ci); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(unsigned)sys(&m->u1,&m->u2,(BOOLEAN)m->ci); }
static char* mk(int n){ char* s=malloc(n+1); for(int i=0;i<n;i++)s[i]='a'+(i&15); s[n]=0; return s; }
int main(void){
    wia_upcase_ansi_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlCompareString");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* Ncs[]={"8/cs","32/cs","128/cs","512/cs","4096/cs","32000/cs"};
    static const char* Nci[]={"8/CI","32/CI","128/CI","512/CI","4096/CI","32000/CI"};
    enum{K=6}; static ctx_t cx[2*K]; static wia_case cs[2*K];
    for(int i=0;i<K;++i){ char*a=mk(L[i]),*b=mk(L[i]);
        for(int m=0;m<2;m++){ ctx_t*c=&cx[m*K+i]; c->u1.Length=c->u1.MaximumLength=(unsigned short)L[i]; c->u1.Buffer=a; c->u2.Length=c->u2.MaximumLength=(unsigned short)L[i]; c->u2.Buffer=b; c->ci=(unsigned char)m;
            wia_case*w=&cs[m*K+i]; w->label=(m?Nci:Ncs)[i]; w->bytes=L[i]; w->ours=op_ours; w->system=op_sys; w->ctx=c; } }
    return wia_bench_compare("RtlCompareString  (wia AVX2 vs ntdll; cs + CI)", cs, 2*K, 200);
}
