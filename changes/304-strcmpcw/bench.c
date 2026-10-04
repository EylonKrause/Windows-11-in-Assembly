/* changes/304-strcmpcw/bench.c
 * Gate 2: wia_strcmp{c,ic,nc,nic}w against live shlwapi!StrCmpCW / StrCmpICW / StrCmpNCW / StrCmpNICW.
 * Equal strings are the long rows, where the export's one-unit loop costs 0.34-0.56 ns each. The
 * short and early-difference rows are where a sort spends most of its compares, and where the export
 * is fastest relative to any setup this pays.
 *
 * HOW THE CALLS ARE MADE, because on the 2 ns rows it decides the verdict (probes/selfcontrol.c):
 *
 *   * every timed op is REP = 16 calls, so the harness's own per-op cost is amortised;
 *   * every (function, side) pair has its OWN call site -- a memory-indirect call through a volatile
 *     pointer, like a caller's `call [__imp_StrCmpCW]` -- and that site only ever sees one target.
 *
 * The second point was learned the hard way. One shared wrapper calling whichever pointer the case
 * held made its indirect call POLYMORPHIC as soon as two functions had gone through it, and from then on
 * every target cost about 3 cycles more: identical code measured 1.42 ns while that call site had seen
 * only it, and 2.08 ns at any of 62 other addresses afterwards; the same function timed 64 times in a
 * row through a site that saw nothing else held 1.62-1.67 ns throughout. Before that, a version that
 * called ours DIRECTLY from a switch and the export through a pointer read "differ at 0" as 0.87x on
 * every run. Neither measured the functions. A real caller's import call is monomorphic, so that is
 * what is timed here, for both sides alike. Table ns are per op, i.e. per 16 calls.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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
SITE2(ocw) SITE2(scw) SITE2(oicw) SITE2(sicw) SITE3(oncw) SITE3(sncw) SITE3(onicw) SITE3(snicw)

int main(void) {
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    p_scw = (PCMP)GetProcAddress(h, "StrCmpCW");    p_sicw = (PCMP)GetProcAddress(h, "StrCmpICW");
    p_sncw = (PCMPN)GetProcAddress(h, "StrCmpNCW"); p_snicw = (PCMPN)GetProcAddress(h, "StrCmpNICW");
    p_ocw = wia_strcmpcw; p_oicw = wia_strcmpicw; p_oncw = wia_strcmpncw; p_onicw = wia_strcmpnicw;
    wia_op OURS[4] = { op_ocw, op_oicw, op_oncw, op_onicw }, SYS[4] = { op_scw, op_sicw, op_sncw, op_snicw };

    /* f, n, length, position of the difference (-1: equal), b upper-cased, label */
    struct { int f; int n; int len; int diff; int upper; const char* label; } R[] = {
        { 0, 0,    0, -1, 0, "CW equal 0"         }, { 0, 0,    1, -1, 0, "CW equal 1"        },
        { 0, 0,    2, -1, 0, "CW equal 2"         }, { 0, 0,    3, -1, 0, "CW equal 3"        },
        { 0, 0,    4, -1, 0, "CW equal 4"         }, { 0, 0,    6, -1, 0, "CW equal 6"        },
        { 0, 0,    8, -1, 0, "CW equal 8"         }, { 0, 0,   16, -1, 0, "CW equal 16"       },
        { 0, 0,   64, -1, 0, "CW equal 64"        }, { 0, 0,  256, -1, 0, "CW equal 256"      },
        { 0, 0, 1024, -1, 0, "CW equal 1024"      }, { 0, 0, 4096, -1, 0, "CW equal 4096"     },
        { 0, 0,   16,  0, 0, "CW differ at 0"     }, { 0, 0,   16,  1, 0, "CW differ at 1"    },
        { 0, 0,   16,  3, 0, "CW differ at 3"     }, { 0, 0,   16,  5, 0, "CW differ at 5"    },
        { 0, 0,  256, 40, 0, "CW differ at 40"    },
        { 1, 0,    1, -1, 1, "ICW case-equal 1"   }, { 1, 0,    3, -1, 1, "ICW case-equal 3"  },
        { 1, 0,    6, -1, 1, "ICW case-equal 6"   }, { 1, 0,   16, -1, 1, "ICW case-equal 16" },
        { 1, 0,  256, -1, 1, "ICW case-equal 256" }, { 1, 0, 4096, -1, 1, "ICW case-equal 4096" },
        { 1, 0,   16,  0, 1, "ICW differ at 0"    }, { 1, 0,   16,  3, 1, "ICW differ at 3"   },
        { 1, 0,   16,  5, 1, "ICW differ at 5"    },
        { 2, 1,  256, -1, 0, "NCW n=1 of 256"     }, { 2, 3,  256, -1, 0, "NCW n=3 of 256"    },
        { 2, 8,  256, -1, 0, "NCW n=8 of 256"     }, { 2, 256, 256, -1, 0, "NCW n=256"         },
        { 2, 4096, 4096, -1, 0, "NCW n=4096"      }, { 2, 64,   16,  5, 0, "NCW differ at 5"   },
        { 3, 3,  256, -1, 1, "NICW n=3 of 256"    }, { 3, 8,  256, -1, 1, "NICW n=8 of 256"   },
        { 3, 256, 256, -1, 1, "NICW n=256"        }, { 3, 64,   16,  5, 1, "NICW differ at 5"  },
    };
    enum { K = sizeof R / sizeof R[0] };
    static wchar_t A[K][4200], B[K][4200];
    static CASE cs[K];
    static wia_case wc[K];
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < R[i].len; ++j) A[i][j] = B[i][j] = (wchar_t)(L'a' + j % 26);
        A[i][R[i].len] = B[i][R[i].len] = 0;
        if (R[i].upper) for (int j = 0; j < R[i].len; j += 2) B[i][j] -= 32;
        if (R[i].diff >= 0) B[i][R[i].diff] = L'#';
        cs[i].n = R[i].n; cs[i].a = A[i]; cs[i].b = B[i];
        wc[i].label = R[i].label; wc[i].bytes = (size_t)(R[i].len ? R[i].len : 1) * 4 * REP;
        wc[i].ours = OURS[R[i].f]; wc[i].system = SYS[R[i].f]; wc[i].ctx = &cs[i];
    }
    return wia_bench_compare("shlwapi StrCmpCW / ICW / NCW / NICW  (wia: 4-unit scalar head, page-bounded 16-unit blocks; ns per 16 calls)", wc, K, 300);
}
