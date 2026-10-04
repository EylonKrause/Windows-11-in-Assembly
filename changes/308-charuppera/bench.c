/* changes/308-charuppera/bench.c
 * Gate 2: the four functions against live user32!CharUpperBuffA / CharLowerBuffA / CharUpperA /
 * CharLowerA. Case mapping is idempotent and the export does the same work on mapped text (it always
 * converts, maps and converts back), so each case runs on one buffer with no restore. Call shape as in
 * 304: 16 calls per timed op, each (function, side) through its own monomorphic call site. Table ns
 * are per 16 calls.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef DWORD (WINAPI *PBUFF)(LPSTR, DWORD);
typedef LPSTR (WINAPI *PSTR_)(LPSTR);
extern DWORD wia_charupperbuffa(LPSTR, DWORD);
extern DWORD wia_charlowerbuffa(LPSTR, DWORD);
extern LPSTR wia_charuppera(LPSTR);
extern LPSTR wia_charlowera(LPSTR);
int wia_cua_init(void);

typedef struct { char* p; DWORD n; } CASE;
#define REP 16
#define SITEB(NAME) static PBUFF volatile p_##NAME; \
    static __declspec(noinline) uint64_t op_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; \
        for (int j = 0; j < REP; ++j) acc += p_##NAME(k->p, k->n); return acc; }
#define SITES(NAME) static PSTR_ volatile p_##NAME; \
    static __declspec(noinline) uint64_t op_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; \
        for (int j = 0; j < REP; ++j) acc += (uint64_t)(uintptr_t)p_##NAME(k->p); return acc; }
SITEB(oub) SITEB(sub) SITEB(olb) SITEB(slb) SITES(ous) SITES(sus) SITES(ols) SITES(sls)

int main(void) {
    if (!wia_cua_init()) { printf("TABLES FAILED\n"); return 3; }
    HMODULE u = LoadLibraryW(L"user32.dll");
    p_sub = (PBUFF)GetProcAddress(u, "CharUpperBuffA"); p_slb = (PBUFF)GetProcAddress(u, "CharLowerBuffA");
    p_sus = (PSTR_)GetProcAddress(u, "CharUpperA");     p_sls = (PSTR_)GetProcAddress(u, "CharLowerA");
    p_oub = wia_charupperbuffa; p_olb = wia_charlowerbuffa; p_ous = wia_charuppera; p_ols = wia_charlowera;
    wia_op O[4] = { op_oub, op_olb, op_ous, op_ols }, S[4] = { op_sub, op_slb, op_sus, op_sls };
    /* fn: 0 UpperBuff 1 LowerBuff 2 Upper string 3 Lower string 4 Upper char mode; kind: 0 ascii, 1 Western */
    struct { int fn; int n; int kind; const char* label; } R[] = {
        { 0, 1, 0, "UpperBuff 1"           }, { 0, 8, 0, "UpperBuff 8"            }, { 0, 16, 0, "UpperBuff 16"   },
        { 0, 64, 0, "UpperBuff 64"         }, { 0, 256, 0, "UpperBuff 256"        }, { 0, 4096, 0, "UpperBuff 4096" },
        { 0, 256, 1, "UpperBuff 256 Western" }, { 0, 4096, 1, "UpperBuff 4096 Western" },
        { 1, 16, 0, "LowerBuff 16"         }, { 1, 256, 0, "LowerBuff 256"        }, { 1, 4096, 1, "LowerBuff 4096 Western" },
        { 2, 0, 0, "Upper string 0"        }, { 2, 8, 0, "Upper string 8"         }, { 2, 64, 0, "Upper string 64" },
        { 2, 256, 0, "Upper string 256"    }, { 2, 4096, 1, "Upper string 4096 Western" },
        { 3, 16, 0, "Lower string 16"      }, { 3, 256, 1, "Lower string 256 Western" },
    };
    enum { K = sizeof R / sizeof R[0] };
    static char buf[K][4200];
    static CASE cs[K];
    static wia_case wc[K];
    static const char W[] = "Les \xE9t\xE9s \xE0 B\xE2le, Gr\xFC\xDF" "e aus K\xF6ln: \xC7" "a \xE7" "a ";
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < R[i].n; ++j) buf[i][j] = R[i].kind ? W[j % (sizeof W - 1)] : (char)('a' + j % 26);
        buf[i][R[i].n] = 0;
        cs[i].p = buf[i]; cs[i].n = (DWORD)R[i].n;
        wc[i].label = R[i].label; wc[i].bytes = (size_t)(R[i].n ? R[i].n : 1) * REP;
        wc[i].ours = O[R[i].fn]; wc[i].system = S[R[i].fn]; wc[i].ctx = &cs[i];
    }
    return wia_bench_compare("user32 CharUpperA/CharLowerA/CharUpperBuffA/CharLowerBuffA  (wia: page-touch, ASCII vector rule, export-built byte tables; ns per 16 calls)", wc, K, 300);
}
