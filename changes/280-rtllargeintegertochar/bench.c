/* changes/280-rtllargeintegertochar/bench.c
 *
 * Gate 2: time wia_lint2char against the live ntdll!RtlLargeIntegerToChar.
 *
 * THE ROWS ARE THE BASES, THE DIGIT COUNTS, THE TWO LENGTH RULES AND -- the one this change adds
 * over 279 -- WHETHER THE VALUE NEEDS THE 64-BIT DIVISION AT ALL:
 *
 *   * a decimal value BELOW 2^32 never enters the eight-digit peel: it goes straight to 067's
 *     proved 32-bit constant. A bench of nineteen-digit values alone would never measure that path,
 *     and a bench of small ones alone would never measure the 64-bit reciprocal.
 *   * ONE peel (nine to sixteen digits) and TWO peels (seventeen to twenty) are separate rows,
 *     because the cost of this converter is the number of times round that loop.
 *   * bases 2, 8 and 16 emit several digits per store; base 2 runs to SIXTY-FOUR characters, which
 *     is the longest answer this export can produce and twice what change 279 could.
 *   * a POSITIVE length writes a terminator only if it fits; a NEGATIVE one is a zero-padded field
 *     width, a fill loop no positive length reaches.
 *
 * discovery/rtl_integer_char.c measured the shipped export at 27.75 ns for nineteen decimal digits.
 * Rows are timed x8, because change 261's probes/floor.c put an empty call through this harness at
 * 2.32 ns and the shortest row here is a single digit.
 *
 * THE PRE-FLIGHT IS THE POINT OF THE TABLE. Every row is run once through the LIVE export before
 * anything is timed, and the status and the bytes it actually wrote are printed. Change 269 shipped
 * a row called "Unicode digits" that the parser refused and reported the refusal as a 30x win, and
 * change 279's "a refusal: no room" row was not a refusal at all until the pre-flight said so.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *F_LI2C)(LARGE_INTEGER*, ULONG, LONG, char*);

extern NTSTATUS wia_lint2char(const LARGE_INTEGER*, ULONG, LONG, char*);
static F_LI2C sys;

typedef struct { LARGE_INTEGER v; ULONG base; LONG len; int reps; char buf[320]; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i)
        acc += (uint64_t)(unsigned long)wia_lint2char(&m->v, m->base, m->len, m->buf) + m->buf[0];
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i)
        acc += (uint64_t)(unsigned long)sys(&m->v, m->base, m->len, m->buf) + m->buf[0];
    return acc;
}

enum { K = 18 };

int main(void)
{
    static const unsigned long long V[K] = {
        1234567890123456789ull,            /* 19 digits: two peels */
        0xFFFFFFFFFFFFFFFFull,             /* 20 digits: two peels, the longest decimal answer */
        1234567890ull,                     /* 10 digits, below 2^32: NO peel at all */
        7ull,                              /* one digit */
        0ull,                              /* zero */
        123456789012ull,                   /* 12 digits: ONE peel */
        0xFFFFFFFFFFFFFFFFull,             /* base 16, sixteen digits */
        0x123ull,                          /* base 16, three digits */
        0xFFFFFFFFFFFFFFFFull,             /* base 8, twenty-two digits */
        0xFFFFFFFFFFFFFFFFull,             /* base 2, SIXTY-FOUR digits */
        1234567890123456789ull,            /* base 0, means 10 */
        1234567890123456789ull,            /* exact room, no terminator */
        0xFFFFFFFFFFFFFFFFull,             /* a field width that fits */
        1234567890123456789ull,            /* zero-padded to 40 */
        1234567890123456789ull,            /* zero-padded to 100 */
        0xFFFFFFFFFFFFFFFFull,             /* base 2 padded to 100: the widest answer, padded */
        1234567890123456789ull,            /* a refusal: base 7 */
        1234567890123456789ull             /* a refusal: no room */
    };
    static const ULONG B[K] = { 10,10,10,10,10,10, 16,16, 8, 2, 0, 10, 16, 10, 10, 2, 7, 10 };
    static const LONG  L[K] = { 200,200,200,200,200,200, 200,200,200,200,200,
                                19, -20, -40, -100, -100, 200, 5 };
    static const char* N[K] = {
        "base 10, 19 digits", "base 10, 20 digits", "base 10, under 2^32 (no peel)",
        "base 10, 1 digit", "base 10, zero", "base 10, 12 digits (one peel)",
        "base 16, 16 digits", "base 16, 3 digits", "base 8, 22 digits", "base 2, 64 digits",
        "base 0 (means 10)", "exact room, no terminator", "a field width that fits",
        "zero-padded to 40", "zero-padded to 100", "base 2, 64 padded to 100",
        "a refusal: base 7", "a refusal: no room"
    };
    /* 0 = this row must be REFUSED */
    static const int WANT_OK[K] = { 1,1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1, 0,0 };
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, bad = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F_LI2C)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlLargeIntegerToChar");
    if (!sys) { printf("no RtlLargeIntegerToChar\n"); return 1; }

    printf("  pre-flight (what each row reaches, through the LIVE export):\n");
    for (i = 0; i < K; ++i) {
        NTSTATUS st;
        int wrote = 0, j;
        cx[i].v.QuadPart = (long long)V[i];
        cx[i].base = B[i]; cx[i].len = L[i]; cx[i].reps = 8;
        memset(cx[i].buf, '#', sizeof cx[i].buf);
        st = sys(&cx[i].v, B[i], L[i], cx[i].buf);
        for (j = 0; j < 260; ++j) if (cx[i].buf[j] != '#') wrote = j + 1;
        printf("    %-30s %08lX  %3d bytes", N[i], (unsigned long)st, st == 0 ? wrote : 0);
        if (st == 0) printf("  \"%.*s\"", wrote > 40 ? 40 : wrote, cx[i].buf);
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

    return wia_bench_compare("ntdll RtlLargeIntegerToChar  (wia: eight-digit peel on one proved "
                             "64-bit reciprocal, multi-digit power-of-two stores, zero-padded "
                             "field widths, x8 per row)", cs, K, 200);
}
