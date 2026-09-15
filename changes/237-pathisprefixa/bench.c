/* changes/237-pathisprefixa/bench.c
   Gate 2: time wia_pathisprefixa against the live shlwapi!PathIsPrefixA.

   Read-only -- the function writes nothing and has no output buffer -- so neither side pays a
   per-iteration restore and the fixed cost shows through cleanly.

   THE CASE MIX. The cost is the length of the common prefix, so the rows vary that, and they vary the
   one thing that changes what our code has to do: whether the prefix is BYTE-IDENTICAL or only
   FOLD-identical. impl.asm compares the raw bytes first and computes the 44-instruction fold only on
   a block where they differ, so the two are timed at the same length to make that cost visible
   instead of hiding it.

   The TRUE and FALSE rows are separated on purpose. A caller asking "is this path under that one"
   usually gets FALSE, and FALSE is reached by a divergence that can be anywhere -- so a row with an
   early divergence is included as the honest floor, where there is nothing to scan and only fixed
   cost is left. Lengths are COMPUTED, never hardcoded. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int wia_pathisprefixa(const char*, const char*);
typedef BOOL (WINAPI *FN)(LPCSTR, LPCSTR);
static FN sys;

typedef struct { const char* a; const char* b; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k = (CASE*)c; return (uint64_t)wia_pathisprefixa(k->a, k->b); }
static uint64_t op_sys (void* c){ CASE* k = (CASE*)c; return (uint64_t)sys(k->a, k->b); }
#pragma optimize("", on)

static char pool[32768];

/* b is a path of n characters with a separator every 8; a is its first `cut` characters, optionally
   upper-cased so the two agree only after folding, optionally perturbed at `bad` to force FALSE. */
static const char* mk(int off, int n, int upper, int bad)
{
    char* p = pool + off;
    for (int i = 0; i < n; ++i) {
        char c = (i % 8 == 7) ? '\\' : (char)('a' + i % 23);
        if (upper && c != '\\') c = (char)(c - 0x20);
        p[i] = c;
    }
    if (bad >= 0 && bad < n) p[bad] = (p[bad] == 'q' || p[bad] == 'Q') ? 'r' : 'q';
    p[n] = 0;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "PathIsPrefixA");

    enum { N = 8 };
    /* full length of b, length of a (a component boundary, so TRUE), case-fold, divergence in a */
    static const int  BLEN[N] = {  64,   64,  256,  256, 4000, 4000,  256,  256 };
    static const int  ALEN[N] = {  15,   15,  255,  255,  255, 3999,  255,  255 };
    static const int  UPR [N] = {   0,    1,    0,    1,    0,    0,    0,    0 };
    static const int  BAD [N] = {  -1,   -1,   -1,   -1,   -1,   -1,    3,  200 };
    static const char* names[N] = {
        "15 of 64, TRUE",
        "15 of 64, CASE",
        "255 of 256, TRUE",
        "255 of 256, CASE",
        "255 of 4000, TRUE",
        "3999 of 4000, TRUE",
        "256, FALSE at 3",
        "256, FALSE at 200",
    };
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    {
        int cur = 0;
        for (int i = 0; i < N; ++i) {
            const char* b = mk(cur, BLEN[i], 0, -1);
            cur += BLEN[i] + 64;
            const char* a = mk(cur, ALEN[i], UPR[i], BAD[i]);
            cur += ALEN[i] + 64;
            if (cur > 30000) { printf("BENCH SETUP ERROR\n"); return 1; }
            C[i].a = a; C[i].b = b;
            /* bytes actually scanned: to the divergence, or the whole of a */
            bytes[i] = (size_t)(BAD[i] >= 0 ? BAD[i] + 1 : ALEN[i]);
            cs[i].label = names[i]; cs[i].bytes = bytes[i];
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
    }
    return wia_bench_compare("shlwapi PathIsPrefixA (wia AVX2 raw-first fold-compare vs shlwapi)",
                             cs, N, 300);
}
