/* changes/304-strcmpcw/probes/selfcontrol.c: what can bench.c RESOLVE on the 1-3 ns rows?
 *
 * On an empty or one-character string, or a difference in the first unit, both functions read a
 * couple of units, subtract and return: 5-17 instructions, 7-8 cycles including the call. Getting
 * bench.c to measure them at all took three attempts (see its header): a direct call against a pointer
 * call, then one shared POLYMORPHIC call site, both measured the wrappers. bench.c now gives every
 * (function, side) its own monomorphic call site, 16 calls per timed op.
 *
 * That still leaves code placement. Byte-identical code -- the export's own loop, assembled into this
 * repository's object -- measured 1.44 ns linked at one address and 2.11 ns at another, and this
 * change's head measured 1.49 ns in one test binary and 1.69 ns in the next with the same bytes. So the
 * question for the gate, per docs/METHODOLOGY.md, is answered by the export AGAINST ITSELF: two
 * distinct monomorphic call sites, both calling live shlwapi, the same statistic bench.c uses
 * (wia_measure, 300 trials, 16 calls per op), N runs per process, several processes. Every non-tie
 * verdict is the harness. The same runs time ours against the export, so the distributions sit side by
 * side.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

typedef int (WINAPI *PCMP)(PCWSTR, PCWSTR);
typedef int (WINAPI *PCMPN)(PCWSTR, PCWSTR, int);
extern int wia_strcmpcw(PCWSTR, PCWSTR);
extern int wia_strcmpicw(PCWSTR, PCWSTR);
extern int wia_strcmpncw(PCWSTR, PCWSTR, int);
extern int wia_strcmpnicw(PCWSTR, PCWSTR, int);

typedef struct { int n; const wchar_t* a; const wchar_t* b; } CASE;

#define REP 16
#define SITE2(NAME) static PCMP volatile p_##NAME; \
    static __declspec(noinline) uint64_t op_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; \
        for (int j = 0; j < REP; ++j) acc += (uint64_t)p_##NAME(k->a, k->b); return acc; }
#define SITE3(NAME) static PCMPN volatile p_##NAME; \
    static __declspec(noinline) uint64_t op_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; \
        for (int j = 0; j < REP; ++j) acc += (uint64_t)p_##NAME(k->a, k->b, k->n); return acc; }
/* o = ours, s = the export, t = the export again through a second site */
SITE2(ocw) SITE2(scw) SITE2(tcw) SITE2(oicw) SITE2(sicw) SITE2(ticw)
SITE3(oncw) SITE3(sncw) SITE3(tncw) SITE3(onicw) SITE3(snicw) SITE3(tnicw)

#define RUNS   8
#define TRIALS 300            /* exactly what bench.c passes to wia_bench_compare */

int main(void) {
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    p_scw = p_tcw = (PCMP)GetProcAddress(h, "StrCmpCW");      p_sicw = p_ticw = (PCMP)GetProcAddress(h, "StrCmpICW");
    p_sncw = p_tncw = (PCMPN)GetProcAddress(h, "StrCmpNCW");  p_snicw = p_tnicw = (PCMPN)GetProcAddress(h, "StrCmpNICW");
    p_ocw = wia_strcmpcw; p_oicw = wia_strcmpicw; p_oncw = wia_strcmpncw; p_onicw = wia_strcmpnicw;
    wia_op O[4] = { op_ocw, op_oicw, op_oncw, op_onicw }, S[4] = { op_scw, op_sicw, op_sncw, op_snicw }, T[4] = { op_tcw, op_ticw, op_tncw, op_tnicw };

    struct { int f; int n; int len; int diff; int upper; const char* label; } R[] = {
        { 0, 0,   0, -1, 0, "CW equal 0"       }, { 0, 0,   1, -1, 0, "CW equal 1"      },
        { 0, 0,   2, -1, 0, "CW equal 2"       }, { 0, 0,   4, -1, 0, "CW equal 4"      },
        { 0, 0,  16,  0, 0, "CW differ at 0"   }, { 0, 0,  16,  1, 0, "CW differ at 1"  },
        { 0, 0,  16,  5, 0, "CW differ at 5"   }, { 1, 0,   6, -1, 1, "ICW case-equal 6" },
        { 1, 0,  16,  0, 1, "ICW differ at 0"  }, { 1, 0,  16,  3, 1, "ICW differ at 3" },
        { 1, 0,  16,  5, 1, "ICW differ at 5"  }, { 2, 3, 256, -1, 0, "NCW n=3 of 256"  },
        { 2, 8, 256, -1, 0, "NCW n=8 of 256"   }, { 2, 64, 16,  5, 0, "NCW differ at 5" },
        { 3, 3, 256, -1, 1, "NICW n=3 of 256"  }, { 3, 64, 16,  5, 1, "NICW differ at 5" },
        { 0, 0, 256, -1, 0, "CW equal 256"     },
    };
    enum { K = sizeof R / sizeof R[0] };
    static wchar_t A[K][300], B[K][300];
    static CASE cs[K];
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < R[i].len; ++j) A[i][j] = B[i][j] = (wchar_t)(L'a' + j % 26);
        A[i][R[i].len] = B[i][R[i].len] = 0;
        if (R[i].upper) for (int j = 0; j < R[i].len; j += 2) B[i][j] -= 32;
        if (R[i].diff >= 0) B[i][R[i].diff] = L'#';
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
