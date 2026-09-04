// changes/051-rtlfindcharinunicodestring/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
extern NTSTATUS wia_findchar(ULONG, const U*, const U*, USHORT*);
typedef NTSTATUS (WINAPI *fn)(ULONG, const U*, const U*, USHORT*);
static fn sys;
typedef struct { U s; U set; } ctx_t;
// forward find-in-set (flags=0), char absent -> full scan (the vectorized hot path).
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; USHORT p; return (uint64_t)(unsigned)wia_findchar(0,&m->s,&m->set,&p)+p; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; USHORT p; return (uint64_t)(unsigned)sys(0,&m->s,&m->set,&p)+p; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlFindCharInUnicodeString");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    static wchar_t SET[]=L"/\\:";   // 3-char set, none present -> full scan
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        wchar_t* s=malloc((L[i]+1)*2);
        for(int k=0;k<L[i];k++) s[k]=(wchar_t)(L'a'+(k%23));   // no set members
        cx[i].s.Length=(USHORT)(L[i]*2); cx[i].s.MaximumLength=(USHORT)(L[i]*2); cx[i].s.Buffer=s;
        cx[i].set.Length=6; cx[i].set.MaximumLength=6; cx[i].set.Buffer=SET;
        cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("RtlFindCharInUnicodeString  (wia AVX2 fwd set-search vs ntdll, 3-char set, absent)", cs, K, 200);
}
