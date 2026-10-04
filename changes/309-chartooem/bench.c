/* changes/309-chartooem/bench.c
 * Gate 2: the four converters against live user32. The source is never modified and the destination
 * is simply rewritten, so no restore. Call shape as in 304: 16 calls per op, each (function, side)
 * through its own monomorphic call site. Table ns are per 16 calls.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

typedef BOOL (WINAPI *PC2OB)(LPCWSTR, LPSTR, DWORD);
typedef BOOL (WINAPI *PC2O)(LPCWSTR, LPSTR);
typedef BOOL (WINAPI *PO2CB)(LPCSTR, LPWSTR, DWORD);
typedef BOOL (WINAPI *PO2C)(LPCSTR, LPWSTR);
extern BOOL wia_chartooembuffw(LPCWSTR, LPSTR, DWORD);
extern BOOL wia_chartooemw(LPCWSTR, LPSTR);
extern BOOL wia_oemtocharbuffw(LPCSTR, LPWSTR, DWORD);
extern BOOL wia_oemtocharw(LPCSTR, LPWSTR);
int wia_c2o_init(void);

typedef struct { const void* s; void* d; DWORD n; } CASE;
#define REP 16
#define SITE(NAME, T, CALL) static T volatile p_##NAME; \
    static __declspec(noinline) uint64_t op_##NAME(void* c) { CASE* k = (CASE*)c; uint64_t acc = 0; \
        for (int j = 0; j < REP; ++j) acc += (uint64_t)CALL; return acc; }
SITE(oc2ob, PC2OB, p_oc2ob(k->s, k->d, k->n)) SITE(sc2ob, PC2OB, p_sc2ob(k->s, k->d, k->n))
SITE(oc2o,  PC2O,  p_oc2o(k->s, k->d))        SITE(sc2o,  PC2O,  p_sc2o(k->s, k->d))
SITE(oo2cb, PO2CB, p_oo2cb(k->s, k->d, k->n)) SITE(so2cb, PO2CB, p_so2cb(k->s, k->d, k->n))
SITE(oo2c,  PO2C,  p_oo2c(k->s, k->d))        SITE(so2c,  PO2C,  p_so2c(k->s, k->d))

int main(void) {
    if (!wia_c2o_init()) { printf("TABLES FAILED\n"); return 3; }
    HMODULE u = LoadLibraryW(L"user32.dll");
    p_sc2ob = (PC2OB)GetProcAddress(u, "CharToOemBuffW"); p_sc2o = (PC2O)GetProcAddress(u, "CharToOemW");
    p_so2cb = (PO2CB)GetProcAddress(u, "OemToCharBuffW"); p_so2c = (PO2C)GetProcAddress(u, "OemToCharW");
    p_oc2ob = wia_chartooembuffw; p_oc2o = wia_chartooemw; p_oo2cb = wia_oemtocharbuffw; p_oo2c = wia_oemtocharw;
    wia_op O[4] = { op_oc2ob, op_oc2o, op_oo2cb, op_oo2c }, S[4] = { op_sc2ob, op_sc2o, op_so2cb, op_so2c };
    struct { int fn; int n; int kind; const char* label; } R[] = {
        { 0, 8, 0, "CharToOemBuffW 8"     }, { 0, 16, 0, "CharToOemBuffW 16"   }, { 0, 256, 0, "CharToOemBuffW 256" },
        { 0, 4096, 0, "CharToOemBuffW 4096" }, { 0, 256, 1, "CharToOemBuffW 256 accented" },
        { 1, 8, 0, "CharToOemW 8"         }, { 1, 64, 0, "CharToOemW 64"       }, { 1, 1024, 0, "CharToOemW 1024" },
        { 2, 8, 0, "OemToCharBuffW 8"     }, { 2, 16, 0, "OemToCharBuffW 16"   }, { 2, 256, 0, "OemToCharBuffW 256" },
        { 2, 4096, 0, "OemToCharBuffW 4096" }, { 2, 256, 1, "OemToCharBuffW 256 box-drawing" },
        { 3, 8, 0, "OemToCharW 8"         }, { 3, 64, 0, "OemToCharW 64"       }, { 3, 1024, 0, "OemToCharW 1024" },
    };
    enum { K = sizeof R / sizeof R[0] };
    static wchar_t ws[K][4200]; static char bs[K][4200]; static char od[K][4200]; static wchar_t wd[K][4200];
    static CASE cs[K];
    static wia_case wc[K];
    for (int i = 0; i < K; ++i) {
        int n = R[i].n, w = R[i].fn < 2;
        for (int j = 0; j < n; ++j) {
            ws[i][j] = R[i].kind ? (wchar_t)((j % 4 == 1) ? 0xE9 : (j % 4 == 3) ? 0xFC : L'a' + j % 26) : (wchar_t)(L'a' + j % 26);
            bs[i][j] = R[i].kind ? (char)((j % 3 == 1) ? 0xC4 : (j % 3 == 2) ? 0xB3 : 'a' + j % 26) : (char)('a' + j % 26);
        }
        ws[i][n] = 0; bs[i][n] = 0;
        cs[i].s = w ? (const void*)ws[i] : (const void*)bs[i];
        cs[i].d = w ? (void*)od[i] : (void*)wd[i];
        cs[i].n = (DWORD)n;
        wc[i].label = R[i].label; wc[i].bytes = (size_t)n * 3 * REP;
        wc[i].ours = O[R[i].fn]; wc[i].system = S[R[i].fn]; wc[i].ctx = &cs[i];
    }
    return wia_bench_compare("user32 CharToOem/OemToChar (W, Buff and string)  (wia: page-bounded 16-unit blocks, export-built tables; ns per 16 calls)", wc, K, 300);
}
