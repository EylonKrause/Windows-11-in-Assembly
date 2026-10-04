/* changes/310-chartooema/bench.c
 * Gate 2: the four converters against live user32. Source untouched, destination rewritten: no
 * restore. Call shape as in 304: 16 calls per op, each (function, side) through its own monomorphic
 * call site. Table ns are per 16 calls.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

typedef BOOL (WINAPI *PB)(LPCSTR, LPSTR, DWORD);
typedef BOOL (WINAPI *PS)(LPCSTR, LPSTR);
extern BOOL wia_chartooembuffa(LPCSTR, LPSTR, DWORD);
extern BOOL wia_chartooema(LPCSTR, LPSTR);
extern BOOL wia_oemtocharbuffa(LPCSTR, LPSTR, DWORD);
extern BOOL wia_oemtochara(LPCSTR, LPSTR);
int wia_c2oa_init(void);

typedef struct { const char* s; char* d; DWORD n; } CASE;
#define REP 16
#define SITEB(NAME) static PB volatile p_##NAME; static __declspec(noinline) uint64_t op_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; for (int j = 0; j < REP; ++j) acc += p_##NAME(k->s, k->d, k->n); return acc; }
#define SITES(NAME) static PS volatile p_##NAME; static __declspec(noinline) uint64_t op_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; for (int j = 0; j < REP; ++j) acc += p_##NAME(k->s, k->d); return acc; }
SITEB(ocb) SITEB(scb) SITES(ocs) SITES(scs) SITEB(oob) SITEB(sob) SITES(oos) SITES(sos)

int main(void) {
    if (!wia_c2oa_init()) { printf("init failed\n"); return 3; }
    HMODULE u = LoadLibraryW(L"user32.dll");
    p_scb = (PB)GetProcAddress(u, "CharToOemBuffA"); p_scs = (PS)GetProcAddress(u, "CharToOemA");
    p_sob = (PB)GetProcAddress(u, "OemToCharBuffA"); p_sos = (PS)GetProcAddress(u, "OemToCharA");
    p_ocb = wia_chartooembuffa; p_ocs = wia_chartooema; p_oob = wia_oemtocharbuffa; p_oos = wia_oemtochara;
    wia_op O[4] = { op_ocb, op_ocs, op_oob, op_oos }, S[4] = { op_scb, op_scs, op_sob, op_sos };
    struct { int fn; int n; int kind; const char* label; } R[] = {
        { 0, 8, 0, "CharToOemBuffA 8"     }, { 0, 32, 0, "CharToOemBuffA 32"    }, { 0, 256, 0, "CharToOemBuffA 256"  },
        { 0, 4096, 0, "CharToOemBuffA 4096" }, { 0, 256, 1, "CharToOemBuffA 256 accented" },
        { 1, 8, 0, "CharToOemA 8"         }, { 1, 64, 0, "CharToOemA 64"        }, { 1, 1024, 0, "CharToOemA 1024"     },
        { 2, 8, 0, "OemToCharBuffA 8"     }, { 2, 32, 0, "OemToCharBuffA 32"    }, { 2, 256, 0, "OemToCharBuffA 256"  },
        { 2, 4096, 0, "OemToCharBuffA 4096" }, { 2, 256, 1, "OemToCharBuffA 256 box-drawing" },
        { 3, 8, 0, "OemToCharA 8"         }, { 3, 64, 0, "OemToCharA 64"        }, { 3, 1024, 2, "OemToCharA 1024 text with tabs and newlines" },
    };
    enum { K = sizeof R / sizeof R[0] };
    static char src[K][4200], dst[K][4200];
    static CASE cs[K];
    static wia_case wc[K];
    for (int i = 0; i < K; ++i) {
        int n = R[i].n;
        for (int j = 0; j < n; ++j) {
            char c = (char)('a' + j % 26);
            if (R[i].kind == 1) c = (char)((j % 4 == 1) ? 0xE9 : (j % 4 == 3) ? 0xFC : c);
            if (R[i].kind == 1 && R[i].fn >= 2) c = (char)((j % 3 == 1) ? 0xC4 : (j % 3 == 2) ? 0xB3 : 'a' + j % 26);
            if (R[i].kind == 2) c = (char)((j % 40 == 39) ? '\n' : (j % 9 == 0) ? '\t' : c);
            src[i][j] = c;
        }
        src[i][n] = 0;
        cs[i].s = src[i]; cs[i].d = dst[i]; cs[i].n = (DWORD)n;
        wc[i].label = R[i].label; wc[i].bytes = (size_t)n * 2 * REP;
        wc[i].ours = O[R[i].fn]; wc[i].system = S[R[i].fn]; wc[i].ctx = &cs[i];
    }
    return wia_bench_compare("user32 CharToOemA/OemToCharA (Buff and string)  (wia: page-bounded 32-byte blocks, identity copy or 32 table loads; ns per 16 calls)", wc, K, 300);
}
