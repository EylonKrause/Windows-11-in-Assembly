/* changes/287-getstringtypew/bench.c
 *
 * Gate 2: time wia_getstringtypew against the live kernelbase!GetStringTypeW.
 *
 * THE ROWS ARE WHAT A CLASSIFICATION LOOKUP ACTUALLY COSTS, AND THE STRING'S COMPOSITION IS AS MUCH OF
 * THE INPUT AS ITS LENGTH:
 *
 *   * ASCII, which is what real text is, and which touches only the first 512 bytes of the table;
 *   * LATIN-1 across the whole 0..255 range, the same 512 bytes but all of them;
 *   * CJK, which reads from the far side of a 128 KB table, so the cache behaviour is the worst case;
 *   * ALTERNATING Latin-1 and CJK, which was written when the implementation still had a per-unit branch
 *     on U+0100 and would mispredict on every character. That branch is gone -- measured, it cost more
 *     than the second load it avoided -- and this row now shows the result: it times the same as all the
 *     others, which is itself the evidence that nothing in the loop depends on the input's composition;
 *   * SURROGATES, their own range and their own class;
 *   * all three info types, because they are three separate 128 KB tables;
 *   * cch = -1, which adds a length scan;
 *   * and short strings, where the whole cost is the call and the table-base setup.
 *
 * The pre-flight confirms the live export agrees with us on every row before timing it, so a row can
 * never report a speed for a call that computes something different.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef BOOL (WINAPI *FGST)(DWORD, LPCWCH, int, LPWORD);

int wia_getstringtypew(DWORD, const wchar_t*, int, unsigned short*);
int wia_gst_init(void);

static FGST sys;

typedef struct { DWORD kind; const wchar_t* s; int cch; unsigned short* out; int reps; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* x = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < x->reps; ++i) acc += (uint64_t)wia_getstringtypew(x->kind, x->s, x->cch, x->out);
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* x = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < x->reps; ++i) acc += (uint64_t)(sys(x->kind, x->s, x->cch, x->out) ? 1 : 0);
    return acc;
}

enum { K = 10 };

static wchar_t ascii[512], latin[512], cjk[512], alt[512], surr[512], sh16[17];
static unsigned short out[600];

int main(void)
{
    static ctx_t cx[K];
    static wia_case cs[K];
    static const char* NAME[K] = {
        "CT1, 511 ASCII",
        "CT1, 511 Latin-1 across 0..255",
        "CT1, 511 CJK (far side of the table)",
        "CT1, 511 ALTERNATING Latin-1 and CJK",
        "CT1, 511 surrogates",
        "CT2, 511 ASCII",
        "CT3, 511 ASCII",
        "CT3, 511 ALTERNATING Latin-1 and CJK",
        "CT1, 511 ASCII, cch = -1 (length scan)",
        "CT1, 16 ASCII"
    };
    int i, bad = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (FGST)GetProcAddress(LoadLibraryW(L"kernelbase.dll"), "GetStringTypeW");
    if (!sys) sys = (FGST)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "GetStringTypeW");
    if (!sys) { printf("no GetStringTypeW\n"); return 1; }
    if (wia_gst_init()) { printf("the tables disagree with the live export\n"); return 1; }

    for (i = 0; i < 511; ++i) {
        ascii[i] = (wchar_t)(L'a' + (i % 26));
        latin[i] = (wchar_t)(1 + (i % 255));
        cjk[i]   = (wchar_t)(0x4E00 + i);
        alt[i]   = (wchar_t)((i & 1) ? (0x4E00 + i) : (1 + (i % 255)));
        surr[i]  = (wchar_t)(0xD800 + (i % 0x800));
    }
    ascii[511] = latin[511] = cjk[511] = alt[511] = surr[511] = 0;
    for (i = 0; i < 16; ++i) sh16[i] = (wchar_t)(L'a' + i);
    sh16[16] = 0;

    cx[0].kind = 1; cx[0].s = ascii; cx[0].cch = 511;
    cx[1].kind = 1; cx[1].s = latin; cx[1].cch = 511;
    cx[2].kind = 1; cx[2].s = cjk;   cx[2].cch = 511;
    cx[3].kind = 1; cx[3].s = alt;   cx[3].cch = 511;
    cx[4].kind = 1; cx[4].s = surr;  cx[4].cch = 511;
    cx[5].kind = 2; cx[5].s = ascii; cx[5].cch = 511;
    cx[6].kind = 4; cx[6].s = ascii; cx[6].cch = 511;
    cx[7].kind = 4; cx[7].s = alt;   cx[7].cch = 511;
    cx[8].kind = 1; cx[8].s = ascii; cx[8].cch = -1;
    cx[9].kind = 1; cx[9].s = sh16;  cx[9].cch = 16;

    printf("  pre-flight (ours must agree with the live export before a row is timed):\n");
    for (i = 0; i < K; ++i) {
        static unsigned short a[600], b[600];
        int n = (cx[i].cch < 0) ? (int)wcslen(cx[i].s) + 1 : cx[i].cch;
        int q, ok = 1;
        cx[i].out = out; cx[i].reps = 1;
        for (q = 0; q < 600; ++q) a[q] = b[q] = 0xA5A5;
        if (!wia_getstringtypew(cx[i].kind, cx[i].s, cx[i].cch, a)) ok = 0;
        if (!sys(cx[i].kind, cx[i].s, cx[i].cch, b)) ok = 0;
        for (q = 0; q < n && ok; ++q) if (a[q] != b[q]) ok = 0;
        printf("    %-42s %d words  %s\n", NAME[i], n, ok ? "agree" : "DISAGREE");
        if (!ok) ++bad;
        cs[i].label = NAME[i];
        cs[i].bytes = (size_t)n * 2;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (bad) { printf("  %d row(s) disagree -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("kernelbase GetStringTypeW  (wia: a flat 65536-entry table per info type, "
                             "derived from the live export and re-checked against it)",
                             cs, K, 200);
}
