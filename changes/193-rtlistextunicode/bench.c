// changes/193-rtlistextunicode/bench.c
// Gate 2: time wia_istextunicode against the live ntdll!RtlIsTextUnicode.
// The size classes deliberately straddle the 512-byte cap that the disassembly revealed: ntdll's
// cost saturates there, so the classes from 1 KB up all measure the SAME work on both sides.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern int wia_istextunicode(const void*, int, int*);
typedef BOOLEAN (__stdcall *F)(const void*, int, int*);
static F sys;

typedef struct { const void* b; int len; int mask; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; int f=k->mask;
                                  return (uint64_t)wia_istextunicode(k->b,k->len,&f) ^ (uint64_t)(unsigned)f; }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; int f=k->mask;
                                  return (uint64_t)sys(k->b,k->len,&f) ^ (uint64_t)(unsigned)f; }
#pragma optimize("", on)

static unsigned char wide[140000], ansi[140000], mixed[140000];

int main(void){
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    sys = (F)GetProcAddress(h,"RtlIsTextUnicode");

    for(int i=0;i<140000;i++){
        wide[i]  = (i&1) ? 0 : (unsigned char)('a' + (i/2)%26);
        ansi[i]  = (unsigned char)(0x20 + (i%0x5F));
        mixed[i] = (unsigned char)((i*7+3) & 0xFF);
    }

    enum { N = 9 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {
        "16 B wide", "64 B wide", "256 B wide", "508 B wide", "1 KB wide (capped)",
        "4 KB wide (capped)", "64 KB wide (capped)", "508 B ANSI", "508 B random" };
    static const int LEN[N] = { 16, 64, 256, 508, 1024, 4096, 65536, 508, 508 };
    for(int i=0;i<7;i++){ C[i].b = wide; C[i].len = LEN[i]; C[i].mask = -1; }
    C[7].b = ansi;  C[7].len = 508; C[7].mask = -1;
    C[8].b = mixed; C[8].len = 508; C[8].mask = -1;
    static const size_t bytes[] = { 16,64,256,508,512,512,512,508,508 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("ntdll RtlIsTextUnicode (wia AVX2 total-variation vs ntdll scalar)", cs, N, 200);
}
