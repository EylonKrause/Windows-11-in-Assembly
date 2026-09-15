// changes/225-lstrlena/bench.c
// Gate 2: time wia_lstrlena against the live kernelbase!lstrlenA.
// Read-only, so there is nothing to restore between iterations and both sides see the same
// resident, warm subject. Lengths are COMPUTED, never hardcoded.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int wia_lstrlena(const char*);
typedef int (WINAPI *FN)(const char*);
static FN sys;

typedef struct { const char* s; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ return (uint64_t)wia_lstrlena(((CASE*)c)->s); }
static uint64_t op_sys (void* c){ return (uint64_t)sys(((CASE*)c)->s); }
#pragma optimize("", on)

static char pool[16384];   /* the last case starts at 5605 and runs 4000 bytes, so this must clear 9606 */

static const char* mk(int off, int n){
    char* p = pool + off;
    for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
    p[n] = 0;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h,"lstrlenA");

    /* Distinct offsets so the cases do not overwrite one another, and deliberately not all
       32-aligned: the masked first block and the 64-byte realignment peel are both on the
       measured path for real callers. */
    static const char *s8, *s16, *s32, *s64, *s254, *s1k, *s4k, *sun;
    s8   = mk(0,    8);
    s16  = mk(16,  16);
    s32  = mk(48,  32);
    s64  = mk(96,  64);
    s254 = mk(176, 254);
    s1k  = mk(448, 1024);
    s4k  = mk(1600, 4000);
    sun  = mk(5605, 4000);          /* an odd, unaligned start on a long subject */

    enum { N = 8 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"8 bytes","16 bytes","32 bytes","64 bytes","254 bytes",
                                  "1024 bytes","4000 bytes","4000 unaligned"};
    const char* srcs[N] = { s8, s16, s32, s64, s254, s1k, s4k, sun };
    static const size_t bytes[] = { 8,16,32,64,254,1024,4000,4000 };
    for (int i = 0; i < N; ++i) {
        C[i].s = srcs[i];
        cs[i].label = names[i]; cs[i].bytes = bytes[i];
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }
    return wia_bench_compare("kernelbase lstrlenA (wia AVX2 64-byte paired scan vs kernelbase)", cs, N, 300);
}
