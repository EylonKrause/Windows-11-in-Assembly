// changes/038-strpbrk/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern char* wia_strpbrk(const char*, const char*);
typedef char* (__cdecl *fn)(const char*, const char*);
static fn sys;
typedef struct { const char* s; const char* set; } ctx_t;
// Built /Od (see build.bat): strpbrk is pure with loop-invariant args, so /O2 MSVC
// hoists the call out of the timing loop (bogus 0.00 ns). /Od => real call each
// iteration for both sides; symmetric overhead only understates our win.
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)wia_strpbrk(m->s,m->set); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)sys(m->s,m->set); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"strpbrk");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    static const char* SET=" \t\r\n;,";
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        char* s=malloc(L[i]+1);
        for(int k=0;k<L[i];k++) s[k]=(char)('a'+(k%23));   // no set members -> full scan
        s[L[i]]=0;
        cx[i].s=s; cx[i].set=SET;
        cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("strpbrk  (wia AVX2 set-broadcast vs ucrtbase, 6-char set, not found)", cs, K, 200);
}
