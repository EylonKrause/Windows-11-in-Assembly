/* changes/307-pathmakeprettyw/bench.c
 * Gate 2: wia_pathmakeprettyw against live shlwapi!PathMakePrettyW.
 *
 * Every call rewrites its path, so the path is restored from a template -- on a ROTATED buffer, the
 * one the call three back used (change 230's reason: a restore landing on the buffer about to be read
 * is a store the next wide load cannot forward from). Same restore, same loop, both sides. Call shape
 * as in 304: 16 calls per timed op, each side through its own monomorphic call site. Table ns are per
 * 16 calls.
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
SITE(o) SITE(s)

int main(void) {
    if (!wia_pmp_init()) { printf("TABLES FAILED\n"); return 3; }
    p_s = (PFN)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "PathMakePrettyW");
    p_o = wia_pathmakeprettyw;
    static wchar_t T[16][320];
    struct { int len; int kind; const char* label; } R[] = {
        { 8,   0, "upper 8"            }, { 32,  0, "upper 32"           }, { 64,  0, "upper 64"           },
        { 128, 0, "upper 128"          }, { 254, 0, "upper 254"          }, { 300, 0, "upper 300 (cut)"    },
        { 60,  1, "path 60, refused at 3" },  { 254, 2, "254, refused at 250" },
        { 60,  3, "path 60, digits+seps"  },  { 60,  4, "path 60, Latin-1 upper" },
        { 60,  5, "path 60, Cyrillic upper" }, { 1, 0, "upper 1"           },
    };
    enum { K = sizeof R / sizeof R[0] };
    static CASE cs[K];
    static wchar_t buf[K][ROT][320];
    static wia_case wc[K];
    for (int i = 0; i < K; ++i) {
        wchar_t* t = T[i];
        int n = R[i].len;
        for (int j = 0; j < n; ++j) {
            switch (R[i].kind) {
            case 0: t[j] = (j % 9 == 2) ? L'\\' : (wchar_t)(L'A' + j % 26); break;
            case 1: case 2: t[j] = (wchar_t)(L'A' + j % 26); break;
            case 3: t[j] = (j % 4 == 3) ? L'\\' : (wchar_t)(L'0' + j % 10); break;
            case 4: t[j] = (j % 5 == 4) ? L'\\' : (wchar_t)(0xC0 + j % 23); break;
            default: t[j] = (j % 5 == 4) ? L'\\' : (wchar_t)(0x410 + j % 32); break;
            }
        }
        if (R[i].kind == 1) t[3] = L'q';
        if (R[i].kind == 2) t[250] = L'q';
        t[n] = 0;
        cs[i].tpl = t; cs[i].bytes = (size_t)(n + 1) * 2; cs[i].idx = 0;
        for (int r = 0; r < ROT; ++r) { cs[i].b[r] = buf[i][r]; memcpy(buf[i][r], t, cs[i].bytes); }
        wc[i].label = R[i].label; wc[i].bytes = (size_t)n * 2 * REP;
        wc[i].ours = op_o; wc[i].system = op_s; wc[i].ctx = &cs[i];
    }
    return wia_bench_compare("shlwapi PathMakePrettyW  (wia: vector refusal scan, ASCII vector map, LCMapStringW-built tables; ns per 16 calls)", wc, K, 300);
}
