// changes/210-comparestringordinal/bench.c
// Gate 2: time wia_comparestringordinal against the live kernelbase!CompareStringOrdinal.
//
// Both modes are timed, because they are different code: case-sensitive is a plain code-unit
// compare, while ignore-case folds ASCII in the vector path and drops to the 64K ordinal upcase
// table whenever a chunk holds anything above 0x7F. The NON-ASCII class exists precisely to keep
// that fallback honest; an implementation that only ever benchmarked ASCII would hide it.
//
// And until 2026-09-16 the row that said so did not do it. Every pair here was built with
// `B[i] = A[i]` (the two strings IDENTICAL) and the implementation's first tier is
//
//      equal raw  =>  equal folded, in any alphabet  =>  advance 16
//
// which fires BEFORE the 0x7F test that would send the chunk to the table. So the row labelled
// "(table path)" measured tier one on Cyrillic input, and the fallback it existed to keep honest
// had never been timed. discovery/cmpordinal_foldpath.c is where that was found; the fix is the
// two `*_CASE` pairs below, which differ only in CASE and therefore fold equal the hard way.
//
// The identical-string rows are kept, because they are a real case and they are what tier one is
// for. They are simply no longer the only thing being measured.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"

extern int wia_comparestringordinal(const wchar_t*, int, const wchar_t*, int, BOOL);
extern void wia_upcase_init(void);
typedef int (WINAPI *FN)(LPCWCH, int, LPCWCH, int, BOOL);
static FN sys;

typedef struct { const wchar_t* a; const wchar_t* b; int n; int ic; int reps; } CASE;

/* The short rows are timed x16, which is change 261's remedy for this harness's own floor.
 *
 * An empty call through wia_bench_compare costs 2.32 ns on this machine (261's probes/floor.c
 * proved it), so a 13-character compare (whose real work is about three nanoseconds) is more
 * than forty percent harness. It shows: adding two rows to the END of this file moved the 13-row
 * from 5.36 to 6.12 ns and both 4000-character ci rows from 121 to 152, on inputs and code that had
 * not changed at all. Sixteen calls per timed iteration puts the work above the floor and the row
 * starts measuring the function instead of the call.
 */
#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; uint64_t s=0; int i;
    for (i = 0; i < k->reps; ++i) s += (uint64_t)wia_comparestringordinal(k->a,k->n,k->b,k->n,k->ic);
    return s; }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; uint64_t s=0; int i;
    for (i = 0; i < k->reps; ++i) s += (uint64_t)sys(k->a,k->n,k->b,k->n,k->ic);
    return s; }
#pragma optimize("", on)

static wchar_t A13[16], B13[16], A64[80], B64[80], A4K[4200], B4K[4200], AU[4200], BU[4200];

/* The new pairs are heap-allocated, and that is not a style choice. Declaring four more 8400-byte
   statics moved every array declared above them and shifted rows that had not changed at all:
   "13 chars, ci" went 5.36 -> 6.09 ns and both 4000-character ci rows went 121 -> 152, on identical
   inputs and identical code. That is the same 4K-aliasing family that parked changes 142, 228, 230
   and 241, and that moved a survey row by 2x when a 16 KB local was added to its main().
   A benchmark that measures its own .bss layout is not measuring the function. */
static wchar_t *AC, *BC, *AUC, *BUC;   /* differ only in CASE */

static void fill(wchar_t* p, int n, int base){
    for (int i = 0; i < n; ++i) p[i] = (wchar_t)(base + (i % 26));
    p[n] = 0;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h, "CompareStringOrdinal");
    wia_upcase_init();

    fill(A13, 13, L'a'); fill(B13, 13, L'a');
    fill(A64, 64, L'a'); fill(B64, 64, L'a');
    fill(A4K, 4000, L'a'); fill(B4K, 4000, L'a');
    for (int i = 0; i < 4000; ++i) { AU[i] = (wchar_t)(0x0430 + (i % 26)); BU[i] = AU[i]; }
    AU[4000] = BU[4000] = 0;
    /* the two that actually fold: same letters, different case, so every chunk differs raw and
       must be folded before it can be called equal */
    AC  = (wchar_t*)malloc(4200 * sizeof(wchar_t));
    BC  = (wchar_t*)malloc(4200 * sizeof(wchar_t));
    AUC = (wchar_t*)malloc(4200 * sizeof(wchar_t));
    BUC = (wchar_t*)malloc(4200 * sizeof(wchar_t));
    for (int i = 0; i < 4000; ++i) {
        AC[i]  = (wchar_t)(L'a'   + (i % 26)); BC[i]  = (wchar_t)(L'A'   + (i % 26));
        AUC[i] = (wchar_t)(0x0430 + (i % 26)); BUC[i] = (wchar_t)(0x0410 + (i % 26));
    }
    AC[4000] = BC[4000] = AUC[4000] = BUC[4000] = 0;

    enum { N = 10 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {
        "13 chars, cs (x16)", "13 chars, ci (x16)", "64 chars, cs (x16)", "64 chars, ci (x16)",
        "4000 chars, cs", "4000 chars, ci", "4000 Cyrillic, ci (tier 1)", "-1 lengths, 4000, cs",
        "4000 ASCII, ci, CASE-differing", "4000 Cyrillic, ci, CASE-differing (the table path)" };
    C[0].a=A13; C[0].b=B13; C[0].n=13;   C[0].ic=0;
    C[1].a=A13; C[1].b=B13; C[1].n=13;   C[1].ic=1;
    C[2].a=A64; C[2].b=B64; C[2].n=64;   C[2].ic=0;
    C[3].a=A64; C[3].b=B64; C[3].n=64;   C[3].ic=1;
    C[4].a=A4K; C[4].b=B4K; C[4].n=4000; C[4].ic=0;
    C[5].a=A4K; C[5].b=B4K; C[5].n=4000; C[5].ic=1;
    C[6].a=AU;  C[6].b=BU;  C[6].n=4000; C[6].ic=1;
    C[7].a=A4K; C[7].b=B4K; C[7].n=-1;   C[7].ic=0;
    C[8].a=AC;  C[8].b=BC;  C[8].n=4000; C[8].ic=1;
    C[9].a=AUC; C[9].b=BUC; C[9].n=4000; C[9].ic=1;

    static const size_t bytes[] = { 26, 26, 128, 128, 8000, 8000, 8000, 8000, 8000, 8000 };
    /* anything whose own work is under about five nanoseconds is timed sixteen times per
       iteration -- see the note above op_ours */
    for (int i = 0; i < N; ++i) C[i].reps = (bytes[i] <= 128) ? 16 : 1;
    for (int i = 0; i < N; ++i) {
        cs[i].label = names[i]; cs[i].bytes = bytes[i] * (size_t)C[i].reps;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }
    return wia_bench_compare("kernelbase CompareStringOrdinal (wia AVX2 vs the shipped compare)",
                             cs, N, 300);
}
