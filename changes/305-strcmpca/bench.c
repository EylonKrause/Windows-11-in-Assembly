/* changes/305-strcmpca/bench.c
 * Gate 2: wia_strcmp{c,ic,nc,nic}a against live shlwapi!StrCmpCA / StrCmpICA / StrCmpNCA / StrCmpNICA.
 * The call shape is 304's, for the reasons recorded there and in docs/METHODOLOGY.md: 16 calls per
 * timed op, and every (function, side) pair has its own monomorphic call site -- an indirect call
 * through a volatile pointer, like a caller's `call [__imp_StrCmpCA]`. Table ns are per 16 calls.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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
SITE2(oca) SITE2(sca) SITE2(oica) SITE2(sica) SITE3(onca) SITE3(snca) SITE3(onica) SITE3(snica)

int main(void) {
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    p_sca = (PCMP)GetProcAddress(h, "StrCmpCA");    p_sica = (PCMP)GetProcAddress(h, "StrCmpICA");
    p_snca = (PCMPN)GetProcAddress(h, "StrCmpNCA"); p_snica = (PCMPN)GetProcAddress(h, "StrCmpNICA");
    p_oca = wia_strcmpca; p_oica = wia_strcmpica; p_onca = wia_strcmpnca; p_onica = wia_strcmpnica;
    wia_op OURS[4] = { op_oca, op_oica, op_onca, op_onica }, SYS[4] = { op_sca, op_sica, op_snca, op_snica };

    /* f, n, length, position of the difference (-1: equal), b upper-cased, label */
    struct { int f; int n; int len; int diff; int upper; const char* label; } R[] = {
        { 0, 0,    0, -1, 0, "CA equal 0"         }, { 0, 0,    1, -1, 0, "CA equal 1"        },
        { 0, 0,    2, -1, 0, "CA equal 2"         }, { 0, 0,    3, -1, 0, "CA equal 3"        },
        { 0, 0,    4, -1, 0, "CA equal 4"         }, { 0, 0,    8, -1, 0, "CA equal 8"        },
        { 0, 0,   16, -1, 0, "CA equal 16"        }, { 0, 0,   32, -1, 0, "CA equal 32"       },
        { 0, 0,   64, -1, 0, "CA equal 64"        }, { 0, 0,  256, -1, 0, "CA equal 256"      },
        { 0, 0, 1024, -1, 0, "CA equal 1024"      }, { 0, 0, 4096, -1, 0, "CA equal 4096"     },
        { 0, 0,   32,  0, 0, "CA differ at 0"     }, { 0, 0,   32,  1, 0, "CA differ at 1"    },
        { 0, 0,   32,  3, 0, "CA differ at 3"     }, { 0, 0,   32,  6, 0, "CA differ at 6"    },
        { 0, 0,  256, 40, 0, "CA differ at 40"    },
        { 1, 0,    1, -1, 1, "ICA case-equal 1"   }, { 1, 0,    3, -1, 1, "ICA case-equal 3"  },
        { 1, 0,    8, -1, 1, "ICA case-equal 8"   }, { 1, 0,   32, -1, 1, "ICA case-equal 32" },
        { 1, 0,  256, -1, 1, "ICA case-equal 256" }, { 1, 0, 4096, -1, 1, "ICA case-equal 4096" },
        { 1, 0,   32,  0, 1, "ICA differ at 0"    }, { 1, 0,   32,  3, 1, "ICA differ at 3"   },
        { 1, 0,   32,  6, 1, "ICA differ at 6"    },
        { 2, 1,  256, -1, 0, "NCA n=1 of 256"     }, { 2, 3,  256, -1, 0, "NCA n=3 of 256"    },
        { 2, 8,  256, -1, 0, "NCA n=8 of 256"     }, { 2, 24, 256, -1, 0, "NCA n=24 of 256"   },
        { 2, 256, 256, -1, 0, "NCA n=256"         }, { 2, 4096, 4096, -1, 0, "NCA n=4096"      },
        { 2, 64,   32,  6, 0, "NCA differ at 6"   },
        { 3, 3,  256, -1, 1, "NICA n=3 of 256"    }, { 3, 8,  256, -1, 1, "NICA n=8 of 256"   },
        { 3, 256, 256, -1, 1, "NICA n=256"        }, { 3, 64,   32,  6, 1, "NICA differ at 6"  },
    };
    enum { K = sizeof R / sizeof R[0] };
    static char A[K][4200], B[K][4200];
    static CASE cs[K];
    static wia_case wc[K];
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < R[i].len; ++j) A[i][j] = B[i][j] = (char)('a' + j % 26);
        A[i][R[i].len] = B[i][R[i].len] = 0;
        if (R[i].upper) for (int j = 0; j < R[i].len; j += 2) B[i][j] -= 32;
        if (R[i].diff >= 0) B[i][R[i].diff] = '#';
        cs[i].n = R[i].n; cs[i].a = A[i]; cs[i].b = B[i];
        wc[i].label = R[i].label; wc[i].bytes = (size_t)(R[i].len ? R[i].len : 1) * 2 * REP;
        wc[i].ours = OURS[R[i].f]; wc[i].system = SYS[R[i].f]; wc[i].ctx = &cs[i];
    }
    return wia_bench_compare("shlwapi StrCmpCA / ICA / NCA / NICA  (wia: 6-byte scalar head (4 counted), one xmm block, page-bounded 32-byte blocks; ns per 16 calls)", wc, K, 300);
}
