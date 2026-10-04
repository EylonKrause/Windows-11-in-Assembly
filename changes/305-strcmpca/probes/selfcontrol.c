/* changes/305-strcmpca/probes/selfcontrol.c: what can bench.c RESOLVE on the 1-3 ns rows?
 *
 * 304's control, for the byte forms (see changes/304-strcmpcw/probes/selfcontrol.c for why it exists):
 * the export timed against ITSELF through two distinct monomorphic call sites, with the gate's own
 * statistic (wia_measure, 300 trials, 16 calls per op), N runs; every non-tie verdict there is the
 * harness. The same runs time ours against the export through bench.c's call shape.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

typedef int (WINAPI *PCMP)(PCSTR, PCSTR);
typedef int (WINAPI *PCMPN)(PCSTR, PCSTR, int);
extern int wia_strcmpca(PCSTR, PCSTR);
extern int wia_strcmpica(PCSTR, PCSTR);
extern int wia_strcmpnca(PCSTR, PCSTR, int);
extern int wia_strcmpnica(PCSTR, PCSTR, int);

typedef struct { int n; const char* a; const char* b; } CASE;

#define REP 16
#define SITE2(NAME) static PCMP volatile p_##NAME; \
    static __declspec(noinline) uint64_t op_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; \
        for (int j = 0; j < REP; ++j) acc += (uint64_t)p_##NAME(k->a, k->b); return acc; }
#define SITE3(NAME) static PCMPN volatile p_##NAME; \
    static __declspec(noinline) uint64_t op_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; \
        for (int j = 0; j < REP; ++j) acc += (uint64_t)p_##NAME(k->a, k->b, k->n); return acc; }
/* o = ours, s = the export, t = the export again through a second site */
SITE2(oca) SITE2(sca) SITE2(tca) SITE2(oica) SITE2(sica) SITE2(tica)
SITE3(onca) SITE3(snca) SITE3(tnca) SITE3(onica) SITE3(snica) SITE3(tnica)

#define RUNS   8
#define TRIALS 300            /* exactly what bench.c passes to wia_bench_compare */

int main(void) {
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    p_sca = p_tca = (PCMP)GetProcAddress(h, "StrCmpCA");      p_sica = p_tica = (PCMP)GetProcAddress(h, "StrCmpICA");
    p_snca = p_tnca = (PCMPN)GetProcAddress(h, "StrCmpNCA");  p_snica = p_tnica = (PCMPN)GetProcAddress(h, "StrCmpNICA");
    p_oca = wia_strcmpca; p_oica = wia_strcmpica; p_onca = wia_strcmpnca; p_onica = wia_strcmpnica;
    wia_op O[4] = { op_oca, op_oica, op_onca, op_onica }, S[4] = { op_sca, op_sica, op_snca, op_snica }, T[4] = { op_tca, op_tica, op_tnca, op_tnica };

    struct { int f; int n; int len; int diff; int upper; const char* label; } R[] = {
        { 0, 0,   0, -1, 0, "CA equal 0"       }, { 0, 0,   1, -1, 0, "CA equal 1"      },
        { 0, 0,   3, -1, 0, "CA equal 3"       }, { 0, 0,   4, -1, 0, "CA equal 4"      },
        { 0, 0,   8, -1, 0, "CA equal 8"       }, { 0, 0,  32,  0, 0, "CA differ at 0"  },
        { 0, 0,  32,  6, 0, "CA differ at 6"   }, { 1, 0,   3, -1, 1, "ICA case-equal 3" },
        { 1, 0,  32,  0, 1, "ICA differ at 0"  }, { 1, 0,  32,  6, 1, "ICA differ at 6" },
        { 2, 1, 256, -1, 0, "NCA n=1 of 256"   }, { 2, 3, 256, -1, 0, "NCA n=3 of 256"  },
        { 2, 64, 32,  6, 0, "NCA differ at 6"  }, { 3, 3, 256, -1, 1, "NICA n=3 of 256" },
        { 3, 64, 32,  6, 1, "NICA differ at 6" }, { 0, 0, 256, -1, 0, "CA equal 256"    },
    };
    enum { K = sizeof R / sizeof R[0] };
    static char A[K][300], B[K][300];
    static CASE cs[K];
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < R[i].len; ++j) A[i][j] = B[i][j] = (char)('a' + j % 26);
        A[i][R[i].len] = B[i][R[i].len] = 0;
        if (R[i].upper) for (int j = 0; j < R[i].len; j += 2) B[i][j] -= 32;
        if (R[i].diff >= 0) B[i][R[i].diff] = '#';
        cs[i].n = R[i].n; cs[i].a = A[i]; cs[i].b = B[i];
    }
    volatile uint64_t sink = 0;
    wia_pin(2);
    printf("%d runs, min-of-%d per side, 16 calls per op, one monomorphic call site per function and side.\n"
           "self = the export against itself through two sites; vs = ours against the export.\n\n", RUNS, TRIALS);
    printf("%-18s %-30s %-30s\n", "row", "self: min/max ratio, non-tie", "vs: min/max ratio, WORSE");
    for (int i = 0; i < K; ++i) {
        int f = R[i].f;
        double smin = 1e9, smax = 0, vmin = 1e9, vmax = 0; int snt = 0, vw = 0;
        for (int r = 0; r < RUNS; ++r) {
            double t = wia_measure(T[f], &cs[i], TRIALS, &sink), s = wia_measure(S[f], &cs[i], TRIALS, &sink);
            double q = s / t; if (q < smin) smin = q; if (q > smax) smax = q; if (q <= 0.97 || q >= 1.03) ++snt;
            double o = wia_measure(O[f], &cs[i], TRIALS, &sink), s2 = wia_measure(S[f], &cs[i], TRIALS, &sink);
            double v = s2 / o; if (v < vmin) vmin = v; if (v > vmax) vmax = v; if (v <= 0.97) ++vw;
        }
        printf("%-18s %5.2fx / %5.2fx  %d of %d       %5.2fx / %5.2fx  %d of %d\n", R[i].label, smin, smax, snt, RUNS, vmin, vmax, vw, RUNS);
    }
    return 0;
}
