// changes/154-strncpy-s/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern int wia_strncpy_s(char*, size_t, const char*, size_t);
typedef int (__cdecl *fn)(char*, size_t, const char*, size_t);
static fn sys;
typedef struct { char* d; size_t size; const char* s; size_t count; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; return (uint64_t)(unsigned)wia_strncpy_s(k->d,k->size,k->s,k->count); }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; return (uint64_t)(unsigned)sys(k->d,k->size,k->s,k->count); }
#pragma optimize("", on)
static char dst[8192], src[8192];
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"strncpy_s");
    memset(src,'a',sizeof src); src[sizeof src - 1]=0;
    static const int L[]={7,15,31,63,254,1024,4096,254};
    static const char* N[]={"7 chars","15 chars","31 chars","63 chars","254 chars",
                            "1024 chars","4096 chars","254 chars, _TRUNCATE truncating"};
    static CASE C[8]; static wia_case cs[8];
    for(int i=0;i<8;++i){
        C[i].d=dst; C[i].size=sizeof dst; C[i].s=src + (sizeof src - 1 - L[i]); C[i].count=(size_t)-1;
        cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    /* the first seven use _TRUNCATE with room to spare (the common "just copy it" call); the last
       forces the truncating branch, which writes `size` bytes and terminates the final one */
    C[7].size=128;
    return wia_bench_compare("ucrtbase strncpy_s (wia AVX2 vs ucrtbase)", cs, 8, 300);
}
