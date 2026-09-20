// changes/299-sysallocstring/bench.c
// Gate 2: time wia_sysallocstring against the live oleaut32!SysAllocString across size classes.
//
// EVERY ITERATION FREES ITS BSTR. A benchmark that leaks one allocation per call measures the heap
// growing, not the function -- the early batches look fast, the later ones slow, and a min-of-N
// reports the warm-up rather than the steady state. The free is inside both sides equally, so it
// cancels out of the ratio while keeping the allocator in the same state for both.
//
// THE SMALL ROWS ARE THE ONES THAT MATTER for the verdict. At 0-16 characters the scan is a single
// block and the allocation dominates, so ours and the export should be within noise; the gate
// requires no size class to regress, and those are the classes where a regression could hide.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include "bench.h"

extern BSTR wia_sysallocstring(const wchar_t*);
typedef BSTR (WINAPI *sas_fn)(const OLECHAR*);
static sas_fn sys_sas;

typedef struct { const wchar_t* s; } strctx;

static uint64_t op_ours(void* c) {
    BSTR b = wia_sysallocstring(((strctx*)c)->s);
    uint64_t v = (uint64_t)(size_t)b;
    SysFreeString(b);
    return v;
}
static uint64_t op_system(void* c) {
    BSTR b = sys_sas(((strctx*)c)->s);
    uint64_t v = (uint64_t)(size_t)b;
    SysFreeString(b);
    return v;
}

static const wchar_t* make_str(size_t len) {
    wchar_t* s = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    for (size_t i = 0; i < len; ++i) s[i] = (wchar_t)(L'a' + (i & 15));
    s[len] = 0;
    return s;
}

int main(void) {
    HMODULE h = LoadLibraryW(L"oleaut32.dll");
    sys_sas = (sas_fn)GetProcAddress(h, "SysAllocString");
    if (!sys_sas) { printf("no SysAllocString\n"); return 2; }

    static const size_t LENS[] = { 0, 4, 16, 32, 64, 128, 256, 512, 1024, 4096 };
    enum { N = sizeof LENS / sizeof LENS[0] };
    static strctx ctx[N];
    static char   lbl[N][32];
    static wia_case cases[N];

    for (int i = 0; i < N; ++i) {
        ctx[i].s = make_str(LENS[i]);
        sprintf_s(lbl[i], sizeof lbl[i], "%zu chars", LENS[i]);
        cases[i].label  = lbl[i];
        cases[i].bytes  = LENS[i] * 2;
        cases[i].ours   = op_ours;
        cases[i].system = op_system;
        cases[i].ctx    = &ctx[i];
    }

    return wia_bench_compare("oleaut32 SysAllocString  (wia AVX2 scan + the real SysAllocStringLen)",
                             cases, N, 40);
}
