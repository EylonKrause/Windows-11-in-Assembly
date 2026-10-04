/* changes/307-pathmakeprettyw/probes/selfcontrol.c: what can bench.c RESOLVE on its closest rows?
 *
 * The narrowest row is a refusal in the first block ("path 60, refused at 3"): both functions read a
 * few units and return 0, and most of the measured time is the template restore both sides share. As in
 * changes/304-strcmpcw/probes/selfcontrol.c, the export is timed against ITSELF through two distinct
 * monomorphic call sites with bench.c's call shape and statistic; every non-tie there is the harness.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef BOOL (WINAPI *PFN)(LPWSTR);
extern BOOL wia_pathmakeprettyw(LPWSTR);
int wia_pmp_init(void);

#define ROT 4
typedef struct { const wchar_t* tpl; size_t bytes; wchar_t* b[ROT]; unsigned idx; } CASE;
#define REP 16
#define SITE(NAME) static PFN volatile p_##NAME; \
    static __declspec(noinline) uint64_t op_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; \
        for (int j = 0; j < REP; ++j) { unsigned i = k->idx, prev = (i + ROT - 1) & (ROT - 1); \
            memcpy(k->b[prev], k->tpl, k->bytes); k->idx = (i + 1) & (ROT - 1); \
            acc += (uint64_t)p_##NAME(k->b[i]); } return acc; }
SITE(o) SITE(s) SITE(t)

int main(void) {
    if (!wia_pmp_init()) return 3;
    p_s = p_t = (PFN)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "PathMakePrettyW");
    p_o = wia_pathmakeprettyw;
    static wchar_t T[3][320], buf[3][ROT][320];
    const char* L[3] = { "path 60, refused at 3", "upper 1", "upper 8" };
    int len[3] = { 60, 1, 8 };
    static CASE cs[3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < len[i]; ++j) T[i][j] = (j % 9 == 2 && i) ? L'\\' : (wchar_t)(L'A' + j % 26);
        if (i == 0) T[0][3] = L'q';
        T[i][len[i]] = 0;
        cs[i].tpl = T[i]; cs[i].bytes = (size_t)(len[i] + 1) * 2;
        for (int r = 0; r < ROT; ++r) { cs[i].b[r] = buf[i][r]; memcpy(buf[i][r], T[i], cs[i].bytes); }
    }
    volatile uint64_t sink = 0;
    wia_pin(2);
    printf("8 runs, min-of-300 per side, 16 calls per op.\n%-24s %-28s %-28s\n", "row", "self: min/max, non-tie", "vs: min/max, WORSE");
    for (int i = 0; i < 3; ++i) {
        double smin = 1e9, smax = 0, vmin = 1e9, vmax = 0; int snt = 0, vw = 0;
        for (int r = 0; r < 8; ++r) {
            double t = wia_measure(op_t, &cs[i], 300, &sink), s = wia_measure(op_s, &cs[i], 300, &sink);
            double q = s / t; if (q < smin) smin = q; if (q > smax) smax = q; if (q <= 0.97 || q >= 1.03) ++snt;
            double o = wia_measure(op_o, &cs[i], 300, &sink), s2 = wia_measure(op_s, &cs[i], 300, &sink);
            double v = s2 / o; if (v < vmin) vmin = v; if (v > vmax) vmax = v; if (v <= 0.97) ++vw;
        }
        printf("%-24s %5.2fx / %5.2fx  %d of 8       %5.2fx / %5.2fx  %d of 8\n", L[i], smin, smax, snt, vmin, vmax, vw);
    }
    return 0;
}
