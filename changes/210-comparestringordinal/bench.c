// changes/210-comparestringordinal/bench.c
// Gate 2: time wia_comparestringordinal against the live kernelbase!CompareStringOrdinal.
//
// Both modes are timed, because they are different code: case-sensitive is a plain code-unit
// compare, while ignore-case folds ASCII in the vector path and drops to the 64K ordinal upcase
// table whenever a chunk holds anything above 0x7F. The NON-ASCII class exists precisely to keep
// that fallback honest -- an implementation that only ever benchmarked ASCII would hide it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern int wia_comparestringordinal(const wchar_t*, int, const wchar_t*, int, BOOL);
extern void wia_upcase_init(void);
typedef int (WINAPI *FN)(LPCWCH, int, LPCWCH, int, BOOL);
static FN sys;

typedef struct { const wchar_t* a; const wchar_t* b; int n; int ic; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; return (uint64_t)wia_comparestringordinal(k->a,k->n,k->b,k->n,k->ic); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; return (uint64_t)sys(k->a,k->n,k->b,k->n,k->ic); }
#pragma optimize("", on)

static wchar_t A13[16], B13[16], A64[80], B64[80], A4K[4200], B4K[4200], AU[4200], BU[4200];

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

    enum { N = 8 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {
        "13 chars, cs", "13 chars, ci", "64 chars, cs", "64 chars, ci",
        "4000 chars, cs", "4000 chars, ci", "4000 Cyrillic, ci (table path)", "-1 lengths, 4000, cs" };
    C[0].a=A13; C[0].b=B13; C[0].n=13;   C[0].ic=0;
    C[1].a=A13; C[1].b=B13; C[1].n=13;   C[1].ic=1;
    C[2].a=A64; C[2].b=B64; C[2].n=64;   C[2].ic=0;
    C[3].a=A64; C[3].b=B64; C[3].n=64;   C[3].ic=1;
    C[4].a=A4K; C[4].b=B4K; C[4].n=4000; C[4].ic=0;
    C[5].a=A4K; C[5].b=B4K; C[5].n=4000; C[5].ic=1;
    C[6].a=AU;  C[6].b=BU;  C[6].n=4000; C[6].ic=1;
    C[7].a=A4K; C[7].b=B4K; C[7].n=-1;   C[7].ic=0;

    static const size_t bytes[] = { 26, 26, 128, 128, 8000, 8000, 8000, 8000 };
    for (int i = 0; i < N; ++i) {
        cs[i].label = names[i]; cs[i].bytes = bytes[i];
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }
    return wia_bench_compare("kernelbase CompareStringOrdinal (wia AVX2 vs the shipped compare)",
                             cs, N, 300);
}
