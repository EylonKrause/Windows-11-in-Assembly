/* changes/276-varbstrcmp/bench.c
 *
 * Gate 2: time wia_varbstrcmp against the live oleaut32!VarBstrCmp.
 *
 * THE ROWS THAT DECIDE THIS CHANGE ARE THE ONES IT CANNOT WIN, and they are in the table for that
 * reason. probes/gap.c measured the shipped wrapper's own overhead at 1.25-2.25 ns on top of the
 * CompareStringW it calls -- so any comparison that has to be collated has essentially nothing to
 * give, and only the comparisons that do NOT have to be collated can be won:
 *
 *     equal by content       0.8 ns per character of pure waste, at every length
 *     the same pointer       3208 ns at 4000 characters, for a pointer comparison
 *     differing at 0         33.25 ns whatever the length -- CompareStringW exits early itself
 *     different lengths      the same
 *
 * A bench made only of the first two would report a spectacular geomean and hide every row in
 * doubt. Both kinds are here at every length, and the short equal rows -- below the fast path's
 * sixteen-character threshold, where this implementation is a lean wrapper and nothing more -- are
 * the ones to read first.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

#pragma comment(lib, "oleaut32.lib")

extern long wia_varbstrcmp(BSTR, BSTR, unsigned long, unsigned long);
extern int  wia_vbc_init(void);

typedef struct { BSTR a, b; int reps; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t v = 0;
    int i;
    for (i = 0; i < m->reps; ++i) v += (uint64_t)(unsigned long)
        wia_varbstrcmp(m->a, m->b, LOCALE_USER_DEFAULT, 0);
    return v;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t v = 0;
    int i;
    for (i = 0; i < m->reps; ++i) v += (uint64_t)(unsigned long)
        VarBstrCmp(m->a, m->b, LOCALE_USER_DEFAULT, 0);
    return v;
}

enum { K = 16 };

int main(void)
{
    static wchar_t p[8200], q[8200];
    static const int LENS[] = { 1, 4, 16, 64, 256, 1000, 4000 };
    static char names[K][40];
    static const char* N[K];
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, n, k = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_vbc_init()) { printf("the flag mask failed to build\n"); return 1; }

    for (i = 0; i < 8200; ++i) { p[i] = (wchar_t)(L'a' + (i % 26)); q[i] = p[i]; }

    /* equal by content, at every length */
    for (i = 0; i < (int)(sizeof LENS / sizeof LENS[0]); ++i) {
        n = LENS[i];
        cx[k].a = SysAllocStringLen(p, (UINT)n);
        cx[k].b = SysAllocStringLen(q, (UINT)n);
        cx[k].reps = (n <= 64) ? 8 : 1;
        wsprintfA(names[k], "equal, %d chars", n);
        ++k;
    }
    /* differing at character 0, at every length -- what CompareStringW already does well */
    for (i = 0; i < (int)(sizeof LENS / sizeof LENS[0]); ++i) {
        n = LENS[i];
        q[0] = L'Z';
        cx[k].a = SysAllocStringLen(p, (UINT)n);
        cx[k].b = SysAllocStringLen(q, (UINT)n);
        cx[k].reps = (n <= 64) ? 8 : 1;
        q[0] = p[0];
        wsprintfA(names[k], "differ at 0, %d chars", n);
        ++k;
    }
    /* the same pointer, and two different lengths */
    cx[k].a = SysAllocStringLen(p, 4000); cx[k].b = cx[k].a; cx[k].reps = 1;
    wsprintfA(names[k], "the same pointer, 4000"); ++k;
    cx[k].a = SysAllocStringLen(p, 4000); cx[k].b = SysAllocStringLen(p, 1); cx[k].reps = 8;
    wsprintfA(names[k], "4000 vs 1 character"); ++k;

    printf("  pre-flight (what each row reaches):\n");
    for (i = 0; i < k; ++i) {
        long v = (long)VarBstrCmp(cx[i].a, cx[i].b, LOCALE_USER_DEFAULT, 0);
        N[i] = names[i];
        printf("    %-28s -> %s\n", N[i],
               v == VARCMP_EQ ? "EQ" : v == VARCMP_LT ? "LT" : v == VARCMP_GT ? "GT" : "ERROR");
        if (v != VARCMP_EQ && v != VARCMP_LT && v != VARCMP_GT) {
            printf("      ^^ THIS ROW DOES NOT REACH THE PATH ITS NAME PROMISES\n");
            return 1;
        }
        cs[i].label = N[i];
        cs[i].bytes = (size_t)SysStringByteLen(cx[i].a) * cx[i].reps;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }

    return wia_bench_compare("oleaut32 VarBstrCmp  (wia: an identity and byte-equality fast path "
                             "into the OS collation, vs the shipped export)", cs, k, 200);
}
