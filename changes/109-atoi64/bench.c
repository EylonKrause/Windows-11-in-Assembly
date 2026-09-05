// changes/109-atoi64/bench.c
// wia_atoi64 vs live ucrtbase!_atoi64 across representative input shapes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern long long wia_atoi64(const char*);
typedef long long (*a64f)(const char*);
static a64f sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ return (uint64_t)wia_atoi64((const char*)c); }
static uint64_t op_sys (void*c){ return (uint64_t)sys((const char*)c); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(a64f)GetProcAddress(h,"_atoi64");
    static const char* S[]={ "7", "12345", "-987654321", "9223372036854775807", "   -9223372036854775808", "00000000000000000000000000000042" };
    static const char* N[]={ "1digit", "5digit", "signed9", "i64_max", "ws+i64_min", "leadzeros32" };
    enum{K=6}; static wia_case cs[K];
    for(int i=0;i<K;++i){
        cs[i].label=N[i]; cs[i].bytes=(size_t)strlen(S[i]); cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=(void*)S[i];
    }
    return wia_bench_compare("ucrtbase _atoi64 (wia scalar vs ucrtbase)", cs, K, 300);
}
