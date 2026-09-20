// changes/089-strstr/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bench.h"
extern char* wia_strstr(const char*, const char*);
typedef char* (__cdecl *fn)(const char*, const char*);
static fn sys;
typedef struct { const char* s; const char* n; } ctx_t;
// Built /Od (see build.bat): strstr is pure with loop-invariant args, so /O2 MSVC
// hoists the call out of the timing loop. /Od => a real call each iteration for both
// sides; the identical loop overhead only understates our win.
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)wia_strstr(m->s,m->n); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; return (uint64_t)(uintptr_t)sys(m->s,m->n); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(h,"strstr");
    static const int L[]={8,32,128,512,4096,32000};
    static const char* N[]={"8","32","128","512","4096","32000"};
    // Realistic: search text (lowercase words) for a short token that occurs ONCE, at the
    // very end (worst case for a first-char scan: full haystack traversal either way).
    static const char* NEEDLE="qz9x";     // first char 'q' is uncommon in the text below
    enum{K=6}; static ctx_t cx[K]; static wia_case cs[K];
    for(int i=0;i<K;++i){
        int n=L[i];
        char* s=malloc(n+8);
        for(int k=0;k<n;k++) s[k]=(char)('a'+(k%23));   // no 'q'..'z' collisions with NEEDLE[0]='q' except rare
        // plant the needle once, ending at the last char (found only after a full scan)
        int m=(int)strlen(NEEDLE);
        if(n>=m) for(int k=0;k<m;k++) s[n-m+k]=NEEDLE[k];
        s[n]=0;
        cx[i].s=s; cx[i].n=NEEDLE;
        cs[i].label=N[i]; cs[i].bytes=n; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&cx[i];
    }
    return wia_bench_compare("strstr  (wia AVX2 first-char anchor + verify vs ucrtbase, 4-char needle at end)", cs, K, 200);
}
