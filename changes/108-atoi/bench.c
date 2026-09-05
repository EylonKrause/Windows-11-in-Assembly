// changes/108-atoi/bench.c
// wia_atoi vs live ucrtbase!atoi across representative input shapes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern int wia_atoi(const char*);
typedef int (*atoif)(const char*);
static atoif sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ return (uint64_t)(unsigned)wia_atoi((const char*)c); }
static uint64_t op_sys (void*c){ return (uint64_t)(unsigned)sys((const char*)c); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(atoif)GetProcAddress(h,"atoi");
    static const char* S[]={ "7", "12345", "-987654", "2147483647", "   -2147483648", "0000000000000000000000000000042" };
    static const char* N[]={ "1digit", "5digit", "signed6", "int_max", "ws+int_min", "leadzeros31" };
    enum{K=6}; static wia_case cs[K];
    for(int i=0;i<K;++i){
        cs[i].label=N[i]; cs[i].bytes=(size_t)strlen(S[i]); cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=(void*)S[i];
    }
    return wia_bench_compare("ucrtbase atoi (wia scalar vs ucrtbase)", cs, K, 300);
}
