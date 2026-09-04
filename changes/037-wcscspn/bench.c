// changes/037-wcscspn/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern size_t wia_wcscspn(const wchar_t*, const wchar_t*);
typedef size_t (__cdecl *fn)(const wchar_t*, const wchar_t*);
static fn sys;
typedef struct { const wchar_t* s; const wchar_t* set; } ctx_t;
// Built /Od (see build.bat): wcscspn is pure with loop-invariant args, so /O2 MSVC
// hoists the call out of the timing loop (bogus 0.00 ns). /Od => real call each
// iteration for BOTH sides; symmetric overhead only understates our win.
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)wia_wcscspn(m->s,m->set); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)sys(m->s,m->set); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"wcscspn");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    // representative 6-char delimiter set; haystack has NO member -> full complement span
    static const wchar_t* SET=L" \t\r\n;,";
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        wchar_t* s=malloc((L[i]+1)*2);
        for(int k=0;k<L[i];k++) s[k]=(wchar_t)(L'a'+(k%23));   // none in set
        s[L[i]]=0;
        cx[i].s=s; cx[i].set=SET;
        cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("wcscspn  (wia AVX2 set-broadcast vs ucrtbase, 6-char set, full span)", cs, K, 200);
}
