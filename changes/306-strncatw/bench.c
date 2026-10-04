/* changes/306-strncatw/bench.c
 * Gate 2: wia_strncatw against live shlwapi!StrNCatW.
 *
 * Call shape as in 304: 16 calls per timed op, each side through its own monomorphic call site. Table
 * ns are per 16 calls.
 *
 * The restore, as in change 230: every call appends, so the destination's terminator has to be put
 * back, and where that one store lands decides the short rows. On the buffer the next call scans, it
 * sits inside the scan's first wide load, which cannot be forwarded from a just-written narrow store
 * and waits for it to drain -- while the export's word-at-a-time scan forwards from it cheaply. So the
 * destination rotates over four identical buffers and the store lands on the one the PREVIOUS call
 * dirtied: the same single store, one address apart. main() reprints both shapes on every run.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef PWSTR (WINAPI *PFN)(PWSTR, PCWSTR, int);
extern PWSTR wia_strncatw(PWSTR, PCWSTR, int);

#define ROT 4
typedef struct { int dlen; int n; const wchar_t* s; wchar_t* ds[ROT]; unsigned idx; } CASE;

#define REP 16
#define SITE(NAME) static PFN volatile p_##NAME; \
    static __declspec(noinline) uint64_t op_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; \
        for (int j = 0; j < REP; ++j) { unsigned i = k->idx, prev = (i + ROT - 1) & (ROT - 1); \
            k->ds[prev][k->dlen] = 0; k->idx = (i + 1) & (ROT - 1); \
            acc += (uint64_t)(uintptr_t)p_##NAME(k->ds[i], k->s, k->n); } return acc; } \
    static __declspec(noinline) uint64_t same_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; \
        for (int j = 0; j < REP; ++j) { k->ds[0][k->dlen] = 0; \
            acc += (uint64_t)(uintptr_t)p_##NAME(k->ds[0], k->s, k->n); } return acc; }
SITE(o) SITE(s)

int main(void) {
    p_s = (PFN)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "StrNCatW");
    p_o = wia_strncatw;
    struct { int dlen; int slen; int n; const char* label; } R[] = {
        { 0,    1,   100, "0 + 1"             }, { 0,   16,   100, "0 + 16"            },
        { 8,    8,   100, "8 + 8"             }, { 16,  16,   100, "16 + 16"           },
        { 16,  16,     8, "16 + 16, n=8"      }, { 16,   1,     2, "16 + 1, n=2"       },
        { 40,   1,   100, "40 + 1"            }, { 1,    1,     1, "1 + 1, n=1"        },
        { 64,  64,  1000, "64 + 64"           }, { 256, 256, 1000, "256 + 256"         },
        { 256, 256,  100, "256 + 256, n=100"  }, { 0,  1024, 2000, "0 + 1024"          },
        { 1024, 1024, 4096, "1024 + 1024"     }, { 4096, 4096, 10000, "4096 + 4096"    },
        { 4096,   8,  100, "4096 + 8"         },
    };
    enum { K = sizeof R / sizeof R[0] };
    static wchar_t src[K][4200], dst[K][ROT][8400];
    static CASE cs[K];
    static wia_case wc[K];
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < R[i].slen; ++j) src[i][j] = (wchar_t)(L'a' + j % 26);
        src[i][R[i].slen] = 0;
        for (int r = 0; r < ROT; ++r) {
            for (int j = 0; j < R[i].dlen; ++j) dst[i][r][j] = (wchar_t)(L'A' + j % 26);
            dst[i][r][R[i].dlen] = 0;
            cs[i].ds[r] = dst[i][r];
        }
        cs[i].dlen = R[i].dlen; cs[i].n = R[i].n; cs[i].s = src[i]; cs[i].idx = 0;
        wc[i].label = R[i].label; wc[i].bytes = (size_t)(R[i].dlen + R[i].slen) * 2 * REP;
        wc[i].ours = op_o; wc[i].system = op_s; wc[i].ctx = &cs[i];
    }
    {
        volatile uint64_t sink = 0;
        wia_pin(2);
        printf("the restore's ADDRESS, measured on this run (same work, one address apart; ns per 16 calls):\n");
        for (int i = 0; i < K; ++i) {
            if (R[i].dlen > 64) continue;
            double so = wia_measure(same_o, &cs[i], 60, &sink), sl = wia_measure(same_s, &cs[i], 60, &sink);
            double ro = wia_measure(op_o, &cs[i], 60, &sink), rl = wia_measure(op_s, &cs[i], 60, &sink);
            printf("  %-18s same buffer: ours %7.2f live %7.2f -> %5.2fx   rotated: ours %7.2f live %7.2f -> %5.2fx\n",
                   R[i].label, so, sl, sl / so, ro, rl, rl / ro);
        }
    }
    return wia_bench_compare("shlwapi StrNCatW  (wia: 303's scan and bounded block copy; the restore on a ROTATED destination; ns per 16 calls)", wc, K, 300);
}
