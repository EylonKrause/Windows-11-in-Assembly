// changes/110-strtol/bench.c
// wia_strtol vs live ucrtbase!strtol across representative inputs (base 10 + base 0 hex).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern long wia_strtol(const char*, char**, int);
typedef long (*strtolf)(const char*,char**,int);
static strtolf sys;
typedef struct { const char* s; int base; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; char*e; return (uint64_t)(unsigned long)wia_strtol(m->s,&e,m->base); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; char*e; return (uint64_t)(unsigned long)sys(m->s,&e,m->base); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(strtolf)GetProcAddress(h,"strtol");
    static ctx_t C[]={ {"7",10}, {"12345",10}, {"-987654",10}, {"2147483647",10}, {"0x1abcdef0",0}, {"777777",8} };
    static const char* N[]={ "1digit", "5digit", "signed6", "int_max", "0xhex8", "octal6" };
    enum{K=6}; static wia_case cs[K];
    for(int i=0;i<K;++i){
        cs[i].label=N[i]; cs[i].bytes=(size_t)strlen(C[i].s); cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    return wia_bench_compare("ucrtbase strtol (wia scalar vs ucrtbase)", cs, K, 300);
}
