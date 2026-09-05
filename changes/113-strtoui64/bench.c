// changes/113-strtoui64/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern unsigned long long wia_strtoui64(const char*, char**, int);
typedef unsigned long long (*f)(const char*,char**,int);
static f sys;
typedef struct { const char* s; int base; } ctx_t;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; char*e; return (uint64_t)wia_strtoui64(m->s,&e,m->base); }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; char*e; return (uint64_t)sys(m->s,&e,m->base); }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(f)GetProcAddress(h,"_strtoui64");
    static ctx_t C[]={ {"7",10}, {"12345",10}, {"-1",10}, {"18446744073709551615",10}, {"0xdeadbeefcafe",0}, {"777777",8} };
    static const char* N[]={ "1digit","5digit","neg1","ui64_max","0xhex12","octal6" };
    enum{K=6}; static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=(size_t)strlen(C[i].s); cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ucrtbase _strtoui64 (wia scalar vs ucrtbase)", cs, K, 300);
}
