// changes/227-lstrcpya/bench.c
// Gate 2: time wia_lstrcpya against the live kernelbase!lstrcpyA.
// The destination is rewritten every iteration by both sides, so neither gets a warm-cache edge.
// Lengths are COMPUTED, never hardcoded.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern char* wia_lstrcpya(char*, const char*);
typedef char* (WINAPI *FN)(char*, const char*);
static FN sys;

typedef struct { const char* s; char* d; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; return (uint64_t)(size_t)wia_lstrcpya(k->d, k->s); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; return (uint64_t)(size_t)sys(k->d, k->s); }
#pragma optimize("", on)

static char spool[16384];
static char dpool[16384];

static const char* mk(int off, int n){
    char* p = spool + off;
    for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
    p[n] = 0;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h,"lstrcpyA");
    if (!sys) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                sys = h2 ? (FN)GetProcAddress(h2,"lstrcpyA") : 0; }

    enum { N = 7 };
    static const int LEN[N]  = { 8, 16, 64, 254, 1024, 4000, 4000 };
    static const int SOFF[N] = { 0, 16, 48, 128, 400, 1700, 5706 };  /* the last one unaligned */
    static const int DOFF[N] = { 0, 16, 48, 128, 400, 1700, 5707 };
    static const char* names[] = {"8 bytes","16 bytes","64 bytes","254 bytes",
                                  "1024 bytes","4000 bytes","4000 unaligned both"};
    static CASE C[N]; static wia_case cs[N];
    static size_t bytes[N];
    for (int i = 0; i < N; ++i) {
        C[i].s = mk(SOFF[i], LEN[i]);
        C[i].d = dpool + DOFF[i];
        bytes[i] = (size_t)LEN[i];
        cs[i].label = names[i]; cs[i].bytes = bytes[i];
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }
    return wia_bench_compare("kernelbase lstrcpyA (wia AVX2 page-clamped copy vs kernelbase byte loop)", cs, N, 300);
}
