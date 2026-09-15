// changes/234-pathfindnextcomponenta/bench.c
// Gate 2: time wia_pathfindnextcomponenta against the live shlwapi!PathFindNextComponentA.
// Read-only, so there is nothing to restore between iterations and no per-iteration memcpy for
// either side to pay -- unlike the in-place path helpers, these ratios are not compressed by a
// shared setup cost.
// Lengths are COMPUTED, never hardcoded.
//
// THE CASE MIX. The cost is "how far to the first separator", which is NOT the string length: a
// path whose first separator is at byte 2 costs the same whether it is 16 bytes or 4000. So the
// rows vary the DISTANCE to the separator, and the no-separator rows are the ones that scan the
// whole string.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern char* wia_pathfindnextcomponenta(const char*);
typedef char* (WINAPI *FN)(const char*);
static FN sys;

typedef struct { const char* s; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ return (uint64_t)(size_t)wia_pathfindnextcomponenta(((CASE*)c)->s); }
static uint64_t op_sys (void* c){ return (uint64_t)(size_t)sys(((CASE*)c)->s); }
#pragma optimize("", on)

static char pool[16384];

static const char* mk(int off, int n, int seppos){
    char* p = pool + off;
    for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
    if (seppos >= 0 && seppos < n) p[seppos] = '\\';
    p[n] = 0;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"PathFindNextComponentA");

    enum { N = 7 };
    /* length, and where the first separator sits (-1 = none) */
    static const int LEN[N] = {  16,   64,  254, 4000,   64,  254, 4000 };
    static const int SEP[N] = {   2,    2,    2,    2,   -1,   -1,   -1 };
    static const char* names[] = {"16, sep at 2","64, sep at 2","254, sep at 2","4000, sep at 2",
                                  "64, no sep","254, no sep","4000, no sep"};
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    {
        int cur = 0;
        for (int i = 0; i < N; ++i) {
            C[i].s = mk(cur, LEN[i], SEP[i]);
            cur += LEN[i] + 64;
            if (cur > 16000) { printf("BENCH SETUP ERROR\n"); return 1; }
            /* the work is the distance scanned, not the string length */
            bytes[i] = (size_t)(SEP[i] >= 0 ? SEP[i] + 1 : LEN[i]);
            cs[i].label = names[i]; cs[i].bytes = bytes[i];
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
    }
    return wia_bench_compare("shlwapi PathFindNextComponentA (wia AVX2 fused scan vs shlwapi)", cs, N, 300);
}
