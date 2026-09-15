// changes/235-pathisfilespeca/bench.c
// Gate 2: time wia_pathisfilespeca against the live shlwapi!PathIsFileSpecA.
// Read-only, so neither side pays a per-iteration restore.
// Lengths are COMPUTED, never hardcoded.
//
// THE CASE MIX. The cost is "how far to the first separator, or to the end if there is none", so
// the rows vary that distance. The clean rows -- a real file name with no separator -- are the ones
// a caller actually asks about most, and they are the ones that scan the whole string.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int wia_pathisfilespeca(const char*);
typedef int (WINAPI *FN)(const char*);
static FN sys;

typedef struct { const char* s; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ return (uint64_t)wia_pathisfilespeca(((CASE*)c)->s); }
static uint64_t op_sys (void* c){ return (uint64_t)sys(((CASE*)c)->s); }
#pragma optimize("", on)

static char pool[16384];

static const char* mk(int off, int n, int seppos, char sep){
    char* p = pool + off;
    for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
    if (seppos >= 0 && seppos < n) p[seppos] = sep;
    p[n] = 0;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"PathIsFileSpecA");

    enum { N = 7 };
    static const int  LEN[N] = {  12,   64,  254, 4000,  254,  254, 4000 };
    static const int  SEP[N] = {  -1,   -1,   -1,   -1,    2,  200,   -1 };
    static const char CH [N] = { 'a',  'a',  'a',  'a', '\\',  ':',  'a' };
    static const char* names[] = {"12, clean file name","64, clean","254, clean","4000, clean",
                                  "254, backslash at 2","254, colon at 200","4000, clean (again)"};
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    {
        int cur = 0;
        for (int i = 0; i < N; ++i) {
            C[i].s = mk(cur, LEN[i], SEP[i], CH[i]);
            cur += LEN[i] + 64;
            if (cur > 16000) { printf("BENCH SETUP ERROR\n"); return 1; }
            bytes[i] = (size_t)(SEP[i] >= 0 ? SEP[i] + 1 : LEN[i]);   /* bytes actually scanned */
            cs[i].label = names[i]; cs[i].bytes = bytes[i];
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
    }
    return wia_bench_compare("shlwapi PathIsFileSpecA (wia AVX2 three-way fused scan vs shlwapi)", cs, N, 300);
}
