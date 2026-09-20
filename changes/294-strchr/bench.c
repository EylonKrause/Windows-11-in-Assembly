// changes/294-strchr/bench.c: wia_strchr vs the LIVE exports, across size classes.
//
// Three tables, and a change lands only if none of them reports a regression:
//   [1] needle ABSENT, the scan runs the whole string to the terminator. Worst case, and the
//                         case discovery/momentary_tier2.c measured at 0.0352 ns/byte.
//   [2] needle FOUND at the LAST character, same amount of scanning, but the return takes the
//                         match path rather than the terminator path.
//   [3] needle ABSENT, vs msvcrt!strchr, msvcrt ships its own copy, so the claim "faster than
//                         both hosts" has to be measured against both and not assumed.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"

extern char* wia_strchr(const char*, int);
typedef char* (__cdecl *fn)(const char*, int);

static fn sys_ucrt, sys_msv;

typedef struct { const char* s; int c; } ctx_t;
static uint64_t op_ours(void* p){ ctx_t* x=(ctx_t*)p; return (uint64_t)(uintptr_t)wia_strchr(x->s, x->c); }
static uint64_t op_ucrt(void* p){ ctx_t* x=(ctx_t*)p; return (uint64_t)(uintptr_t)sys_ucrt(x->s, x->c); }
static uint64_t op_msv (void* p){ ctx_t* x=(ctx_t*)p; return (uint64_t)(uintptr_t)sys_msv (x->s, x->c); }

// 'a'..'p' only, so '@' (0x40) is provably absent unless we plant it.
static char* mk(size_t len, int plant_at_end){
    char* s=(char*)malloc(len+1);
    for(size_t i=0;i<len;++i) s[i]=(char)('a'+(i&15));
    if(plant_at_end && len) s[len-1]='@';
    s[len]=0;
    return s;
}

enum { K = 10 };
static const size_t L[K]={1,3,7,15,31,63,255,1023,8191,65535};
static const char*  N[K]={"1","3","7","15","31","63","255","1023","8191","65535"};

int main(void){
    sys_ucrt=(fn)GetProcAddress(LoadLibraryW(L"ucrtbase.dll"),"strchr");
    sys_msv =(fn)GetProcAddress(LoadLibraryW(L"msvcrt.dll"),  "strchr");
    if(!sys_ucrt||!sys_msv){ printf("resolve failed\n"); return 2; }

    static ctx_t ca[K], cb[K];
    static wia_case sa[K], sb[K], sc[K];
    for(int i=0;i<K;++i){
        ca[i].s=mk(L[i],0); ca[i].c='@';
        cb[i].s=mk(L[i],1); cb[i].c='@';
        sa[i].label=N[i]; sa[i].bytes=L[i]; sa[i].ours=op_ours; sa[i].system=op_ucrt; sa[i].ctx=&ca[i];
        sb[i].label=N[i]; sb[i].bytes=L[i]; sb[i].ours=op_ours; sb[i].system=op_ucrt; sb[i].ctx=&cb[i];
        sc[i].label=N[i]; sc[i].bytes=L[i]; sc[i].ours=op_ours; sc[i].system=op_msv;  sc[i].ctx=&ca[i];
    }

    // A benchmark that is silently measuring the wrong thing is worse than no benchmark:
    // assert the three implementations agree on every row before timing any of them.
    for(int i=0;i<K;++i){
        if(wia_strchr(ca[i].s,'@')!=sys_ucrt(ca[i].s,'@') ||
           wia_strchr(ca[i].s,'@')!=sys_msv (ca[i].s,'@') ||
           wia_strchr(cb[i].s,'@')!=sys_ucrt(cb[i].s,'@') ||
           wia_strchr(cb[i].s,'@')!=sys_msv (cb[i].s,'@')){
            printf("BENCH SETUP ERROR: row %s does not agree with the live exports\n", N[i]);
            return 1;
        }
    }

    int rc  = wia_bench_compare(
        "strchr (wia AVX2 vs live ucrtbase!strchr) -- needle ABSENT, scan runs to the terminator",
        sa, K, 200);
    int rc2 = wia_bench_compare(
        "strchr (wia AVX2 vs live ucrtbase!strchr) -- needle FOUND at the LAST character",
        sb, K, 200);
    int rc3 = wia_bench_compare(
        "strchr (wia AVX2 vs live msvcrt!strchr, the second host) -- needle ABSENT",
        sc, K, 200);
    if(rc2) rc=rc2;
    if(rc3) rc=rc3;
    return rc;
}
