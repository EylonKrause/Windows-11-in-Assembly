// bench.h - shared measurement harness for "Windows 11 in Assembly".
// Header-only. Each change's bench.c includes this and calls bench_compare().
//
// Design goals (see docs/METHODOLOGY.md):
//   * compare OUR asm against the LIVE system function on this PC,
//   * across size classes,
//   * pinned core + raised priority + warm cache,
//   * minimum-of-N trials (robust to this machine's bad-RAM timing noise),
//   * print an explicit BETTER / WORSE / TIE verdict per size class and overall.
#ifndef WIA_BENCH_H
#define WIA_BENCH_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <intrin.h>

// A thing the harness can time: run `op` on a prepared input, return an opaque
// result (used to defeat dead-code elimination). ctx is caller state.
typedef uint64_t (*wia_op)(void* ctx);

static double wia_qpc_freq(void) {
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart;
}

// Pin to one core and raise priority so the scheduler and other cores stop
// contaminating the measurement. Core 2 by default (avoid 0/1 = OS chatter).
static void wia_pin(int core) {
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << core);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    Sleep(0);
}

// Minimum wall-nanoseconds-per-call over `trials` trials, each averaging `inner`
// back-to-back calls. Minimum (not mean) because we want the least-perturbed run;
// the bad RAM only ever adds time, never removes it.
static double wia_min_ns(wia_op op, void* ctx, int inner, int trials, volatile uint64_t* sink) {
    double freq = wia_qpc_freq();
    double best = 1e300;
    // warm
    for (int i = 0; i < 8; ++i) *sink ^= op(ctx);
    for (int t = 0; t < trials; ++t) {
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        uint64_t acc = 0;
        for (int i = 0; i < inner; ++i) acc ^= op(ctx);
        QueryPerformanceCounter(&b);
        *sink ^= acc;
        double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / freq / (double)inner;
        if (ns < best) best = ns;
    }
    return best;
}

// One size class: name, and prepared ops for ours vs system over the same input.
typedef struct {
    const char* label;
    size_t      bytes;      // logical size processed per call (for bytes/cycle)
    wia_op      ours;
    wia_op      system;
    void*       ctx;
} wia_case;

// Run every case, print a table with a per-row verdict and an overall verdict.
// Returns 0 if OURS is >= system (within tie band) on EVERY row, else 1.
static int wia_bench_compare(const char* title, wia_case* cases, int n,
                             int inner, int trials) {
    volatile uint64_t sink = 0;
    wia_pin(2);
    printf("\n== BENCH: %s ==\n", title);
    printf("%-14s %12s %12s %9s %10s  %s\n",
           "size class", "ours ns", "system ns", "ratio", "ours GB/s", "verdict");
    printf("--------------------------------------------------------------------------------\n");
    int all_ok = 1;
    double geo = 1.0; int geo_n = 0;
    for (int i = 0; i < n; ++i) {
        double o = wia_min_ns(cases[i].ours,   cases[i].ctx, inner, trials, &sink);
        double s = wia_min_ns(cases[i].system, cases[i].ctx, inner, trials, &sink);
        double ratio = s / o;                 // >1 means ours faster
        double gbs = cases[i].bytes ? (double)cases[i].bytes / o : 0.0; // bytes/ns = GB/s
        const char* verdict;
        if      (ratio >= 1.03) verdict = "BETTER";
        else if (ratio <= 0.97) { verdict = "WORSE"; all_ok = 0; }
        else                     verdict = "~tie";
        printf("%-14s %12.2f %12.2f %8.2fx %9.2f  %s\n",
               cases[i].label, o, s, ratio, gbs, verdict);
        geo *= ratio; ++geo_n;
    }
    printf("--------------------------------------------------------------------------------\n");
    double overall = geo_n ? pow(geo, 1.0 / (double)geo_n) : 1.0;
    printf("overall speed ratio (geomean, >1 = ours faster): %.3fx  => %s\n",
           overall, all_ok ? "LANDS (no size class regressed)" : "PARKED (a size class regressed)");
    printf("sink=%llu\n", (unsigned long long)sink);
    return all_ok ? 0 : 1;
}

#endif // WIA_BENCH_H
