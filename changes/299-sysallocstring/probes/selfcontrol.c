/* changes/299-sysallocstring/probes/selfcontrol.c -- what can this bench actually RESOLVE?
 *
 * The short rows of bench.c are a small difference between two large numbers. At 0, 4 and 16
 * characters the call is 20-25 ns of which almost all is the allocator -- SysAllocStringLen(NULL,n)
 * alone measures 16-25 ns in discovery/oleaut32_sysallocstring.c -- and our contribution, replacing
 * a scalar strlen over a handful of characters, is one to three nanoseconds of that. The ratio is
 * two allocator timings divided by each other. Across four consecutive runs those rows read 1.00x,
 * 1.03x, 0.94x, 1.07x for the empty string and 1.14x, 0.58x, 1.11x, 1.07x for sixteen characters.
 * The 0.58x is not a regression that comes and goes; it is noise.
 *
 * Change 298 hit this and answered it the only way that is not an opinion: MEASURE THE LIVE EXPORT
 * AGAINST ITSELF. Both sides are then literally the same function, so any verdict other than a tie
 * is the harness failing to resolve the row, and counting those gives the resolution floor.
 *
 * THIS MUST USE THE SAME STATISTIC AS THE GATE, and the first version of this file did not -- it
 * timed one batch per side where wia_measure takes the MINIMUM over `trials` auto-calibrated
 * batches. Single batches made every row look unresolvable, including 4096 characters, where the
 * real margin is 7x and unmistakable. A control that is noisier than the thing it is controlling
 * measures nothing. wia_measure is used directly below, with the same trials count bench.c passes.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "bench.h"

typedef BSTR (WINAPI *sas_fn)(const OLECHAR*);
static sas_fn sys_sas;

typedef struct { const wchar_t* s; } strctx;

/* Both sides call the same export, through two distinct call sites so neither the compiler nor
   the branch predictor can treat them as one. */
static uint64_t op_a(void* c) {
    BSTR b = sys_sas(((strctx*)c)->s);
    uint64_t v = (uint64_t)(size_t)b; SysFreeString(b); return v;
}
static uint64_t op_b(void* c) {
    BSTR b = sys_sas(((strctx*)c)->s);
    uint64_t v = (uint64_t)(size_t)b; SysFreeString(b); return v;
}

static const wchar_t* make_str(size_t len) {
    wchar_t* s = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    for (size_t i = 0; i < len; ++i) s[i] = (wchar_t)(L'a' + (i & 15));
    s[len] = 0;
    return s;
}

#define RUNS   18
#define TRIALS 40          /* exactly what bench.c passes to wia_bench_compare */

int main(void) {
    HMODULE h = LoadLibraryW(L"oleaut32.dll");
    sys_sas = (sas_fn)GetProcAddress(h, "SysAllocString");
    if (!sys_sas) { printf("no SysAllocString\n"); return 2; }

    static const size_t LENS[] = { 0, 4, 16, 32, 64, 128, 256, 512, 1024, 4096 };
    enum { N = sizeof LENS / sizeof LENS[0] };
    static strctx ctx[N];
    static double worst[N], best[N];
    static int nbad[N];
    volatile uint64_t sink = 0;
    int i, r, runs_with_a_regression = 0;

    for (i = 0; i < N; ++i) { ctx[i].s = make_str(LENS[i]); worst[i] = 1e9; best[i] = 0; nbad[i] = 0; }

    printf("THE EXPORT AGAINST ITSELF -- %d runs, min-of-%d batches per side, the same statistic\n"
           "the gate uses. Every verdict other than a tie is the harness failing to resolve the row.\n\n",
           RUNS, TRIALS);

    wia_pin(2);
    for (r = 0; r < RUNS; ++r) {
        int bad_this_run = 0;
        for (i = 0; i < N; ++i) {
            double a = wia_measure(op_a, &ctx[i], TRIALS, &sink);
            double b = wia_measure(op_b, &ctx[i], TRIALS, &sink);
            double ratio = (a > 0.0) ? b / a : 0.0;     /* "ours" = op_a, "system" = op_b */
            if (ratio < worst[i]) worst[i] = ratio;
            if (ratio > best[i])  best[i]  = ratio;
            if (ratio <= 0.97) { ++nbad[i]; bad_this_run = 1; }
        }
        if (bad_this_run) ++runs_with_a_regression;
    }

    printf("  %-12s %10s %10s %16s\n", "size", "worst", "best", "runs <= 0.97x");
    printf("  ----------------------------------------------------------\n");
    for (i = 0; i < N; ++i)
        printf("  %-12zu %9.2fx %9.2fx %11d of %d%s\n",
               LENS[i], worst[i], best[i], nbad[i], RUNS,
               nbad[i] ? "   <- UNRESOLVABLE" : "");

    printf("\n  %d of %d runs reported at least one size class WORSE -- with both sides the same\n"
           "  function. That is this gate's false-regression rate on this subject.\n",
           runs_with_a_regression, RUNS);
    printf("\nA row that scores at or below 0.97x against ITSELF cannot support a claim either way at\n"
           "that size. A row that never does is resolved, and its verdict in bench.c means something.\n");
    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
