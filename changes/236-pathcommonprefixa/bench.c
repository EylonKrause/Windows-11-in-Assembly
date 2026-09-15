/* changes/236-pathcommonprefixa/bench.c
   Gate 2: time wia_pathcommonprefixa against the live shlwapi!PathCommonPrefixA.

   THE CASE MIX. probes/pcpa.c measured the shipped export at a near-flat 8-11 ns per byte from 16 to
   2048 characters, so the cost is the LENGTH OF THE COMMON PREFIX and nothing else. The rows
   therefore vary that length, and they vary the two things that change what our code has to do:

     * BYTE-IDENTICAL vs CASE-DIFFERING paths. impl.asm compares the raw bytes first and computes the
       44-instruction fold only on a block where they differ, so a byte-identical prefix never pays
       for the fold and a case-differing one pays it per block. Both are timed, at the same length,
       so the cost of the fold is visible rather than hidden.
     * EARLY DIVERGENCE. When the paths differ in the first component there is almost nothing to
       scan and the fixed cost is all that is left.

   The output buffer is supplied on every row, because that is how the function is actually called
   and because the copy is part of the work. Lengths are COMPUTED, never hardcoded. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int wia_pathcommonprefixa(const char*, const char*, char*);
typedef int (WINAPI *FN)(LPCSTR, LPCSTR, LPSTR);
static FN sys;

typedef struct { const char* a; const char* b; char* out; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k = (CASE*)c; return (uint64_t)wia_pathcommonprefixa(k->a, k->b, k->out); }
static uint64_t op_sys (void* c){ CASE* k = (CASE*)c; return (uint64_t)sys(k->a, k->b, k->out); }
#pragma optimize("", on)

static char pool[32768];
static char obuf[8192];

/* Build a path of n characters with a separator every 8, optionally upper-casing the whole thing
   (so the two paths are fold-equal but not byte-equal), optionally diverging at position d. */
static const char* mk(int off, int n, int upper, int d)
{
    char* p = pool + off;
    for (int i = 0; i < n; ++i) {
        char c = (i % 8 == 7) ? '\\' : (char)('a' + i % 23);
        if (upper && c != '\\') c = (char)(c - 0x20);
        p[i] = c;
    }
    if (d >= 0 && d < n) p[d] = (p[d] == 'q' || p[d] == 'Q') ? 'r' : 'q';
    p[n] = 0;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "PathCommonPrefixA");

    enum { N = 8 };
    /* length, whether the second path is upper-cased, and where it diverges (-1 = nowhere) */
    static const int  LEN[N] = {  16,   16,   64,   64,  254,  254, 4000,  254 };
    static const int  UPR[N] = {   0,    1,    0,    1,    0,    1,    0,    0 };
    static const int  DIV[N] = {  -1,   -1,   -1,   -1,   -1,   -1,   -1,    3 };
    static const char* names[N] = {
        "16, identical",
        "16, differing CASE",
        "64, identical",
        "64, differing CASE",
        "254, identical",
        "254, differing CASE",
        "4000, identical",
        "254, diverges at 3",
    };
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    {
        int cur = 0;
        for (int i = 0; i < N; ++i) {
            C[i].a = mk(cur, LEN[i], 0, -1);
            cur += LEN[i] + 64;
            C[i].b = mk(cur, LEN[i], UPR[i], DIV[i]);
            cur += LEN[i] + 64;
            if (cur > 30000) { printf("BENCH SETUP ERROR\n"); return 1; }
            C[i].out = obuf;
            /* bytes actually scanned: the common prefix, or the divergence point */
            bytes[i] = (size_t)(DIV[i] >= 0 ? DIV[i] + 1 : LEN[i]);
            cs[i].label = names[i]; cs[i].bytes = bytes[i];
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
    }
    return wia_bench_compare("shlwapi PathCommonPrefixA (wia AVX2 raw-first fold-compare vs shlwapi)",
                             cs, N, 300);
}
