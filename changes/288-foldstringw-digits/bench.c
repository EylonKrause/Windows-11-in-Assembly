/* changes/288-foldstringw-digits/bench.c
 *
 * Gate 2: time wia_foldstringw_digits against the live kernelbase!FoldStringW with MAP_FOLDDIGITS.
 *
 * The rows separate the three things this call can cost:
 *
 *   * ASCII, where every code unit maps to itself and the table is read from its first 512 bytes;
 *   * ARABIC-INDIC DIGITS, where every code unit actually changes, 462 of 65535 units do, and a row
 *     of nothing but those is the case where the fold is doing real work rather than copying;
 *   * a MIXED string, which is what real text with foreign digits looks like;
 *   * CJK, which reads the far side of a 128 KB table and never changes anything, so it isolates the
 *     cache behaviour from the mapping;
 *   * cchSrc = -1, which adds a length scan;
 *   * cchDest = 0, the LENGTH QUERY, which must not write at all, for us that is pure overhead with no
 *     store loop, and it is worth knowing whether the export short-circuits it as cheaply;
 *   * and a short string, where the whole cost is the call and the argument checking.
 *
 * The pre-flight confirms the live export agrees with us on every row before timing it, so no row can
 * report a speed for a call that computes something different.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef int (WINAPI *FFOLD)(DWORD, LPCWSTR, int, LPWSTR, int);

int wia_foldstringw_digits(DWORD, const wchar_t*, int, wchar_t*, int);
int wia_fold_init(void);

#define MAPD 0x0080

static FFOLD sys;

typedef struct { const wchar_t* s; int cchSrc; int cchDest; wchar_t* out; int reps; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* x = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < x->reps; ++i)
        acc += (uint64_t)wia_foldstringw_digits(MAPD, x->s, x->cchSrc, x->out, x->cchDest);
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* x = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < x->reps; ++i)
        acc += (uint64_t)sys(MAPD, x->s, x->cchSrc, x->out, x->cchDest);
    return acc;
}

enum { K = 7 };

static wchar_t ascii[512], arabic[512], mixed[512], cjk[512], sh16[17];
static wchar_t out[700];

int main(void)
{
    static ctx_t cx[K];
    static wia_case cs[K];
    static const char* NAME[K] = {
        "511 ASCII (nothing changes)",
        "511 ARABIC-INDIC digits (every unit changes)",
        "511 mixed ASCII and foreign digits",
        "511 CJK (far side of the table, no change)",
        "511 ASCII, cchSrc = -1 (length scan)",
        "511 ASCII, cchDest = 0 (length query only)",
        "16 ASCII"
    };
    int i, bad = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (FFOLD)GetProcAddress(LoadLibraryW(L"kernelbase.dll"), "FoldStringW");
    if (!sys) sys = (FFOLD)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "FoldStringW");
    if (!sys) { printf("no FoldStringW\n"); return 1; }
    if (wia_fold_init()) { printf("the table disagrees with the live export\n"); return 1; }

    for (i = 0; i < 511; ++i) {
        ascii[i]  = (wchar_t)(L'a' + (i % 26));
        arabic[i] = (wchar_t)(0x0660 + (i % 10));
        mixed[i]  = (wchar_t)((i % 4) ? (L'a' + (i % 26)) : (0x06F0 + (i % 10)));
        cjk[i]    = (wchar_t)(0x4E00 + i);
    }
    ascii[511] = arabic[511] = mixed[511] = cjk[511] = 0;
    for (i = 0; i < 16; ++i) sh16[i] = (wchar_t)(L'a' + i);
    sh16[16] = 0;

    cx[0].s = ascii;  cx[0].cchSrc = 511; cx[0].cchDest = 700;
    cx[1].s = arabic; cx[1].cchSrc = 511; cx[1].cchDest = 700;
    cx[2].s = mixed;  cx[2].cchSrc = 511; cx[2].cchDest = 700;
    cx[3].s = cjk;    cx[3].cchSrc = 511; cx[3].cchDest = 700;
    cx[4].s = ascii;  cx[4].cchSrc = -1;  cx[4].cchDest = 700;
    cx[5].s = ascii;  cx[5].cchSrc = 511; cx[5].cchDest = 0;
    cx[6].s = sh16;   cx[6].cchSrc = 16;  cx[6].cchDest = 700;

    printf("  pre-flight (ours must agree with the live export before a row is timed):\n");
    for (i = 0; i < K; ++i) {
        static wchar_t a[700], b[700];
        int na, nb, q, ok = 1, n;
        cx[i].out = out; cx[i].reps = 1;
        for (q = 0; q < 700; ++q) a[q] = b[q] = 0xA5A5;
        na = wia_foldstringw_digits(MAPD, cx[i].s, cx[i].cchSrc, cx[i].cchDest ? a : 0, cx[i].cchDest);
        nb = sys(MAPD, cx[i].s, cx[i].cchSrc, cx[i].cchDest ? b : 0, cx[i].cchDest);
        if (na != nb || na <= 0) ok = 0;
        n = na;
        if (ok && cx[i].cchDest)
            for (q = 0; q < n; ++q) if (a[q] != b[q]) { ok = 0; break; }
        printf("    %-46s %d units  %s\n", NAME[i], na, ok ? "agree" : "DISAGREE");
        if (!ok) ++bad;
        cs[i].label = NAME[i];
        cs[i].bytes = (size_t)(na > 0 ? na : 1) * 2;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (bad) { printf("  %d row(s) disagree -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("kernelbase FoldStringW MAP_FOLDDIGITS  (wia: a flat 65536-entry fold "
                             "table derived from the live export and re-checked against it)",
                             cs, K, 200);
}
