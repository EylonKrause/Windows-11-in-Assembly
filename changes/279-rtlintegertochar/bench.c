/* changes/279-rtlintegertochar/bench.c
 *
 * Gate 2: time wia_int2char against the live ntdll!RtlIntegerToChar.
 *
 * The rows are the bases, the digit counts and the two length rules, because those are what the
 * implementation distinguishes:
 *
 *   * base 10 goes through a length-first, two-digits-at-a-time converter;
 *   * bases 2, 8 and 16 emit more than one digit per store from wide tables;
 *   * a POSITIVE length writes the digits and a terminator only if one fits;
 *   * a negative length is a zero-padded field width -- probes/negative.c found it, and it runs a
 *     second loop that nothing else in this bench reaches. A bench without a padded row would
 *     measure two of the three write paths and report them as the function.
 *
 * discovery/rtl_integer_char.c measured the shipped export at 13.62 ns for ten decimal digits and
 * 6.84 for eight hexadecimal. Rows are timed x8, because change 261's probes/floor.c put an empty
 * call through this harness at 2.32 ns and the shortest row here is a single digit.
 *
 * The three refusals are rows for the same reason they are in every bench in this project: a caller
 * hits them as often as the succeeding case, and no "how fast does it format" row measures them.
 *
 * The pre-flight is the point of the table. Every row is run once through the live export before
 * anything is timed, and the status and the bytes it actually wrote are printed. Change 269 shipped
 * a row called "Unicode digits" that the parser refused, and reported the refusal as a 30x win.
 * A row whose outcome does not match its name stops the bench.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *F_I2C)(ULONG, ULONG, LONG, char*);

extern NTSTATUS wia_int2char(ULONG, ULONG, LONG, char*);
static F_I2C sys;

typedef struct { ULONG v, base; LONG len; int reps; char buf[256]; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i)
        acc += (uint64_t)(unsigned long)wia_int2char(m->v, m->base, m->len, m->buf) + m->buf[0];
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i)
        acc += (uint64_t)(unsigned long)sys(m->v, m->base, m->len, m->buf) + m->buf[0];
    return acc;
}

enum { K = 16 };

int main(void)
{
    static const ULONG V[K]  = { 3735928559ul, 7, 3735928559ul, 15, 3735928559ul, 3735928559ul,
                                 4294967295ul, 0, 3735928559ul, 4294967295ul, 3735928559ul,
                                 3735928559ul, 3735928559ul, 3735928559ul, 3735928559ul,
                                 3735928559ul };   /* the last row must NOT fit in 3 bytes; 7 did */
    static const ULONG B[K]  = { 10, 10, 16, 16, 8, 2, 10, 10, 0, 2, 10, 16, 10, 10, 7, 10 };
    static const LONG  L[K]  = { 200, 200, 200, 200, 200, 200, 200, 200, 200, 200,
                                 10, -10, -40, -100, 200, 3 };
    static const char* N[K] = {
        "base 10, 10 digits", "base 10, 1 digit", "base 16, 8 digits", "base 16, 1 digit",
        "base 8, 11 digits", "base 2, 32 digits", "base 10, 4294967295", "base 10, zero",
        "base 0 (means 10)", "base 2, all ones",
        "exact room, no terminator", "a field width that fits",
        "zero-padded to 40", "zero-padded to 100",
        "a refusal: base 7", "a refusal: no room"
    };
    /* 0 = this row must be REFUSED. 14 and 15 are the refusals; everything else must succeed. */
    static const int WANT_OK[K] = { 1,1,1,1,1,1,1,1,1,1, 1,1,1,1, 0,0 };
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, bad = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F_I2C)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlIntegerToChar");
    if (!sys) { printf("no RtlIntegerToChar\n"); return 1; }

    printf("  pre-flight (what each row reaches, through the LIVE export):\n");
    for (i = 0; i < K; ++i) {
        NTSTATUS st;
        int wrote = 0, j;
        cx[i].v = V[i]; cx[i].base = B[i]; cx[i].len = L[i]; cx[i].reps = 8;
        memset(cx[i].buf, '#', sizeof cx[i].buf);
        st = sys(V[i], B[i], L[i], cx[i].buf);
        for (j = 0; j < 200; ++j) if (cx[i].buf[j] != '#') wrote = j + 1;
        printf("    %-26s %08lX  %3d bytes", N[i], (unsigned long)st, st == 0 ? wrote : 0);
        if (st == 0) printf("  \"%.*s\"", wrote > 44 ? 44 : wrote, cx[i].buf);
        printf("\n");
        if ((st == 0) != (WANT_OK[i] != 0)) {
            printf("      ^^ THIS ROW DOES NOT REACH THE PATH ITS NAME PROMISES\n");
            ++bad;
        }
        cs[i].label = N[i];
        cs[i].bytes = (size_t)(st == 0 ? wrote : 8) * cx[i].reps;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll RtlIntegerToChar  (wia: length-first decimal, multi-digit "
                             "power-of-two stores, zero-padded field widths, x8 per row)",
                             cs, K, 200);
}
