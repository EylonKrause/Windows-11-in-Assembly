// changes/035-wcspbrk/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"
extern wchar_t* wia_wcspbrk(const wchar_t*, const wchar_t*);
typedef wchar_t* (__cdecl *fn)(const wchar_t*, const wchar_t*);
static fn sys;
typedef struct { const wchar_t* s; const wchar_t* set; } ctx_t;

// The op wrappers are compiled with optimization OFF so each invocation makes a
// real, un-hoistable call that reads its args from memory. wia_wcspbrk is a pure,
// loop-invariant-arg function; with /O2 MSVC devirtualizes the harness's call and
// hoists it clean out of the timing loop (=> bogus "0.00 ns"), while the system fn
// (an opaque GetProcAddress pointer) is always really called -- an unfair compare.
// Opt-off here forces a real call for both, symmetric and correct. (The harness in
// bench.h stays fully optimized.)
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)wia_wcspbrk(m->s,m->set); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)sys(m->s,m->set); }
#pragma optimize("", on)

int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"wcspbrk");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    // realistic delimiter set, none of which appear in the haystack -> full scan (worst case)
    static const wchar_t* SET=L" \t\r\n;,";
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        wchar_t* s=malloc((L[i]+1)*2);
        for(int k=0;k<L[i];k++) s[k]=(wchar_t)(L'a'+(k%23));   // letters only, no set members
        s[L[i]]=0;
        cx[i].s=s; cx[i].set=SET;
        cs[i].label=N[i]; cs[i].bytes=L[i]*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("wcspbrk  (wia AVX2 set-broadcast vs ucrtbase, 6-char set, not found)", cs, K, 200);
}
