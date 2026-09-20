// bench.h - shared measurement harness for "Windows 11 in Assembly".
// Header-only. Each change's bench.c fills a wia_case[] and calls wia_bench_compare().
//
// Design (see docs/METHODOLOGY.md):
//   * compare OUR asm against the LIVE system function on this PC,
//   * across size classes,
//   * pinned core + raised priority + warm cache,
//   * per case, auto-calibrate the inner iteration count so even a tiny input is
//     timed well above the QueryPerformanceCounter tick (no sub-tick 0/NaN),
//   * minimum-of-N batches (robust to this machine's bad-RAM timing noise),
//   * explicit BETTER / WORSE / ~tie verdict per size class + overall geomean.
#ifndef WIA_BENCH_H
#define WIA_BENCH_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <intrin.h>

typedef uint64_t (*wia_op)(void* ctx);   // run op on ctx, return value (defeats DCE)

static double wia_qpc_freq(void) {
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart;
}

static void wia_pin(int core) {
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << core);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    Sleep(0);
}

// One batch of `inner` calls, wall-ns for the whole batch.
static double wia_batch_ns(wia_op op, void* ctx, int inner, double freq, volatile uint64_t* sink) {
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    uint64_t acc = 0;
    for (int i = 0; i < inner; ++i) acc ^= op(ctx);
    QueryPerformanceCounter(&b);
    *sink ^= acc;
    return (double)(b.QuadPart - a.QuadPart) * 1e9 / freq;
}

// Per-call ns = minimum over `trials` batches, with inner auto-calibrated so a
// batch spans >= ~300us (>> tick). Robust at both tiny and huge input sizes.
static double wia_measure(wia_op op, void* ctx, int trials, volatile uint64_t* sink) {
    double freq = wia_qpc_freq();
    const double target = 300000.0;      // 300 us per batch
    for (int i = 0; i < 16; ++i) *sink ^= op(ctx);   // warm
    int inner = 64;
    for (;;) {
        double ns = wia_batch_ns(op, ctx, inner, freq, sink);
        if (ns >= target || inner >= (1 << 26)) break;
        double factor = ns > 0.0 ? target / ns : 8.0;
        if (factor < 2.0) factor = 2.0; else if (factor > 64.0) factor = 64.0;
        int next = (int)(inner * factor);
        inner = (next <= inner) ? inner * 2 : next;
    }
    double best = 1e300;
    for (int t = 0; t < trials; ++t) {
        double ns = wia_batch_ns(op, ctx, inner, freq, sink) / (double)inner;
        if (ns < best) best = ns;
    }
    return best;
}

typedef struct {
    const char* label;
    size_t      bytes;     // logical bytes processed per call (for GB/s)
    wia_op      ours;
    wia_op      system;
    void*       ctx;
} wia_case;

// Print the table + per-row and overall verdict. Returns 0 iff OURS did not
// regress (>= 0.97x) on every size class.
static int wia_bench_compare(const char* title, wia_case* cases, int n, int trials) {
    volatile uint64_t sink = 0;
    wia_pin(2);
    printf("\n== BENCH: %s ==\n", title);
    printf("%-12s %13s %13s %9s %11s  %s\n",
           "size", "ours ns", "system ns", "ratio", "ours GB/s", "verdict");
    printf("--------------------------------------------------------------------------------\n");
    int all_ok = 1;
    double geo_sum = 0.0; int geo_n = 0;
    for (int i = 0; i < n; ++i) {
        double o = wia_measure(cases[i].ours,   cases[i].ctx, trials, &sink);
        double s = wia_measure(cases[i].system, cases[i].ctx, trials, &sink);
        double ratio = (o > 0.0) ? s / o : 0.0;              // >1 = ours faster
        double gbs = (cases[i].bytes && o > 0.0) ? (double)cases[i].bytes / o : 0.0;
        const char* verdict;
        if      (ratio >= 1.03) verdict = "BETTER";
        else if (ratio <= 0.97) { verdict = "WORSE"; all_ok = 0; }
        else                     verdict = "~tie";
        printf("%-12s %13.2f %13.2f %8.2fx %11.2f  %s\n",
               cases[i].label, o, s, ratio, gbs, verdict);
        if (ratio > 0.0 && isfinite(ratio)) { geo_sum += log(ratio); ++geo_n; }
    }
    printf("--------------------------------------------------------------------------------\n");
    double overall = geo_n ? exp(geo_sum / (double)geo_n) : 0.0;
    printf("overall speed ratio (geomean, >1 = ours faster): %.3fx  => %s\n",
           overall, all_ok ? "LANDS (no size class regressed)" : "PARKED (a size class regressed)");
    printf("sink=%llu\n", (unsigned long long)sink);
    return all_ok ? 0 : 1;
}

#endif // WIA_BENCH_H
