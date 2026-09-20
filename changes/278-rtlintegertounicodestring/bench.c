/* changes/278-rtlintegertounicodestring/bench.c
 *
 * Gate 2: time wia_int2ustr against the live ntdll!RtlIntegerToUnicodeString.
 *
 * The rows are the bases and the digit counts, because that is what the implementation
 * distinguishes: base 10 goes through a length-first, two-digits-at-a-time converter and the three
 * power-of-two bases share a shift-and-mask loop. A bench of base 10 alone would measure one of the
 * two and report it as the function.
 *
 * discovery/rtl_integer_char.c measured the shipped export at 32.14 ns for ten decimal digits,
 * 18.66 for eight hexadecimal, 20.35 for eleven octal, 40.27 for thirty-two binary, and 10.16 ns
 * for a SINGLE decimal digit, which is the row that says how much of the cost is fixed. All five are
 * here.
 *
 * The two refusals are here for the same reason they are in every bench in this project: a caller
 * hits them as often as the succeeding case, and no "how fast does it format" row measures them.
 *
 * Rows are timed x8: the shortest is about 4 ns and change 261's probes/floor.c put an empty call
 * through this harness at 2.32 ns.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef NTSTATUS (NTAPI *F_I2US)(ULONG, ULONG, USTR*);

extern NTSTATUS wia_int2ustr(ULONG, ULONG, USTR*);
static F_I2US sys;

typedef struct { ULONG v, base; USHORT max; int reps; wchar_t buf[64]; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) {
        USTR u;
        u.Length = 0; u.MaximumLength = m->max; u.Buffer = m->buf;
        acc += (uint64_t)(unsigned long)wia_int2ustr(m->v, m->base, &u) + u.Length;
    }
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) {
        USTR u;
        u.Length = 0; u.MaximumLength = m->max; u.Buffer = m->buf;
        acc += (uint64_t)(unsigned long)sys(m->v, m->base, &u) + u.Length;
    }
    return acc;
}

enum { K = 14 };

int main(void)
{
    static const ULONG V[K]    = { 3735928559ul, 7, 3735928559ul, 15, 3735928559ul, 3735928559ul,
                                   4294967295ul, 0, 3735928559ul, 4294967295ul, 3735928559ul,
                                   3735928559ul, 3735928559ul, 3735928559ul };
    static const ULONG B[K]    = { 10, 10, 16, 16, 8, 2, 10, 10, 0, 2, 10, 16, 7, 10 };
    static const USHORT M[K]   = { 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 22, 18, 200, 4 };
    static const char* N[K] = {
        "base 10, 10 digits", "base 10, 1 digit", "base 16, 8 digits", "base 16, 1 digit",
        "base 8, 11 digits", "base 2, 32 digits", "base 10, 4294967295", "base 10, zero",
        "base 0 (means 10)", "base 2, all ones", "base 10, exact room", "base 16, exact room",
        "a refusal: base 7", "a refusal: no room"
    };
    /* 0 = this row must be refused */
    static const int WANT_OK[K] = { 1,1,1,1,1,1,1,1,1,1,1,1, 0,0 };
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, bad = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F_I2US)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlIntegerToUnicodeString");
    if (!sys) { printf("no RtlIntegerToUnicodeString\n"); return 1; }

    printf("  pre-flight (what each row reaches):\n");
    for (i = 0; i < K; ++i) {
        USTR u;
        NTSTATUS st;
        cx[i].v = V[i]; cx[i].base = B[i]; cx[i].max = M[i]; cx[i].reps = 8;
        u.Length = 0; u.MaximumLength = M[i]; u.Buffer = cx[i].buf;
        st = sys(V[i], B[i], &u);
        printf("    %-24s %08lX  %u chars", N[i], (unsigned long)st,
               st == 0 ? u.Length / 2 : 0);
        if (st == 0) printf("  \"%.*ls\"", u.Length / 2, cx[i].buf);
        printf("\n");
        if ((st == 0) != (WANT_OK[i] != 0)) {
            printf("      ^^ THIS ROW DOES NOT REACH THE PATH ITS NAME PROMISES\n");
            ++bad;
        }
        cs[i].label = N[i];
        cs[i].bytes = (size_t)(st == 0 ? u.Length : 8) * cx[i].reps;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll RtlIntegerToUnicodeString  (wia: length-first, no division, "
                             "x8 calls per row)", cs, K, 200);
}
