/* changes/277-charupperbuffw/bench.c
 *
 * Gate 2: time wia_charupperbuffw and wia_charlowerbuffw against the live user32 exports.
 *
 * The rows are the two paths and the lengths that cross between them. a 16-character block with no
 * code unit at or above 0x80 is handled entirely in registers; any block with a high code unit falls
 * back to the table for that block. So a bench of ASCII alone would measure one path and report it
 * as the function, which is the defect change 210 shipped and change 269's first bench had.
 *
 *   ASCII            the in-register range subtract
 *   all high         the table, every block
 *   one high in 64   the worst realistic mixture: one block of 16 falls back, the rest do not
 *   Both directions  because the lower form has its own range ('a'..'z' plus 0x20) and its own
 *                    table, and discovery measured it 50% slower than the upper one
 *
 * Short rows are timed x16: discovery/rtl_integer_char.c put a single character at 8.25 ns and
 * change 261's probes/floor.c put an empty call through this harness at 2.32 ns.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

#pragma comment(lib, "user32.lib")

extern DWORD wia_charupperbuffw(wchar_t*, DWORD);
extern DWORD wia_charlowerbuffw(wchar_t*, DWORD);
extern int   wia_cub_init(void);

typedef struct { wchar_t* buf; DWORD n; int reps; int lower; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t v = 0;
    int i;
    for (i = 0; i < m->reps; ++i)
        v += m->lower ? wia_charlowerbuffw(m->buf, m->n) : wia_charupperbuffw(m->buf, m->n);
    return v;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t v = 0;
    int i;
    for (i = 0; i < m->reps; ++i)
        v += m->lower ? CharLowerBuffW(m->buf, m->n) : CharUpperBuffW(m->buf, m->n);
    return v;
}

enum { K = 20 };

int main(void)
{
    static wchar_t pool[K][4200];
    static char names[K][40];
    static const char* N[K];
    static ctx_t cx[K];
    static wia_case cs[K];
    static const int LENS[] = { 1, 16, 64, 256, 4000 };
    int i, j, k = 0, lower;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_cub_init()) { printf("the case tables failed to build\n"); return 1; }

    for (lower = 0; lower < 2; ++lower) {
        for (i = 0; i < (int)(sizeof LENS / sizeof LENS[0]); ++i) {
            int n = LENS[i];
            /* ASCII */
            for (j = 0; j < n; ++j) pool[k][j] = (wchar_t)((lower ? L'A' : L'a') + (j % 26));
            cx[k].buf = pool[k]; cx[k].n = (DWORD)n; cx[k].lower = lower;
            cx[k].reps = (n <= 64) ? 16 : 1;
            wsprintfA(names[k], "%s ASCII %d", lower ? "lower" : "upper", n);
            ++k;
        }
        /* all-high, and one high code unit in 64 */
        for (j = 0; j < 256; ++j) pool[k][j] = (wchar_t)(0x0100 + (j % 0x300));
        cx[k].buf = pool[k]; cx[k].n = 256; cx[k].lower = lower; cx[k].reps = 1;
        wsprintfA(names[k], "%s all-high 256", lower ? "lower" : "upper");
        ++k;
        for (j = 0; j < 64; ++j) pool[k][j] = (wchar_t)((lower ? L'A' : L'a') + (j % 26));
        pool[k][40] = (wchar_t)0x00E9;
        cx[k].buf = pool[k]; cx[k].n = 64; cx[k].lower = lower; cx[k].reps = 16;
        wsprintfA(names[k], "%s one high in 64", lower ? "lower" : "upper");
        ++k;
        /* count 0, which must touch nothing */
        cx[k].buf = pool[k]; cx[k].n = 0; cx[k].lower = lower; cx[k].reps = 16;
        wsprintfA(names[k], "%s count 0", lower ? "lower" : "upper");
        ++k;
    }

    printf("  pre-flight (what each row reaches):\n");
    for (i = 0; i < k; ++i) {
        wchar_t probe[8];
        DWORD r;
        int hi = 0;
        for (j = 0; j < (int)cx[i].n && j < 4000; ++j)
            if ((unsigned)cx[i].buf[j] >= 0x80) { hi = 1; break; }
        for (j = 0; j < 8; ++j) probe[j] = cx[i].buf[j];
        r = cx[i].lower ? CharLowerBuffW(probe, cx[i].n ? 8 : 0)
                        : CharUpperBuffW(probe, cx[i].n ? 8 : 0);
        printf("    %-22s %5lu chars  %-10s returns %lu\n", names[i], (unsigned long)cx[i].n,
               cx[i].n == 0 ? "(no work)" : (hi ? "table path" : "vector path"), (unsigned long)r);
        N[i] = names[i];
        cs[i].label = N[i];
        cs[i].bytes = (size_t)cx[i].n * 2 * cx[i].reps;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }

    return wia_bench_compare("user32 CharUpperBuffW / CharLowerBuffW  (wia vs the shipped exports)",
                             cs, k, 200);
}
