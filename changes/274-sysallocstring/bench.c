/* changes/274-sysallocstring/bench.c
 *
 * Gate 2: time wia_sysallocstring against the live oleaut32!SysAllocString.
 *
 * EVERY ROW ALLOCATES AND FREES on both sides, through SysFreeString, because the allocation is not
 * ours to make and is most of the cost at short lengths -- probes/where.c put the floor
 * (SysAllocStringLen with the length already known) at 13.25 ns for an empty string.
 *
 * THE SHORT ROWS ARE THE ONES THAT DECIDE WHETHER THIS CHANGE LANDS AT ALL, and they are in the
 * table for exactly that reason. probes/where.c measured the shipped scan at 0.10 ns for an empty
 * string and 0.1997 ns per character at 65000 -- so the long rows have 779 ns to win and the
 * shortest has nothing. A bench that started at 256 characters would report a handsome geomean and
 * hide the only rows in doubt.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

#pragma comment(lib, "oleaut32.lib")

extern BSTR wia_sysallocstring(const wchar_t*);

/* EIGHT ALLOCATIONS PER MEASUREMENT, and the reason is a verdict that changed between runs of the
   SAME BINARY. At one allocation per measurement the short rows are about sixteen nanoseconds of
   which roughly fifteen are the allocator, so the row measures allocator jitter and the scan is
   inside the error bar: five consecutive runs printed the four-character row at 1.03x, 1.05x,
   1.00x, 1.02x and 1.01x, and a sixth printed 0.98x and PARKED the change. That is change 225's
   note -- "anything decided at this scale has to be decided across runs" -- reaching a gate that is
   only allowed one. Eight per measurement puts the row above a hundred nanoseconds and the verdict
   stops moving. */
#define REPS 8

typedef struct { const wchar_t* s; } ctx_t;

static uint64_t op_ours(void* c)
{
    const wchar_t* s = ((ctx_t*)c)->s;
    uint64_t v = 0;
    int i;
    for (i = 0; i < REPS; ++i) {
        BSTR b = wia_sysallocstring(s);
        v += (uint64_t)SysStringLen(b);
        SysFreeString(b);
    }
    return v;
}
static uint64_t op_sys(void* c)
{
    const wchar_t* s = ((ctx_t*)c)->s;
    uint64_t v = 0;
    int i;
    for (i = 0; i < REPS; ++i) {
        BSTR b = SysAllocString(s);
        v += (uint64_t)SysStringLen(b);
        SysFreeString(b);
    }
    return v;
}

enum { K = 12 };

int main(void)
{
    static const int LENS[K] = { 0, 1, 2, 4, 8, 16, 32, 64, 256, 1000, 4000, 16000 };
    static wchar_t pool[K][16100];
    static char names[K][32];
    static const char* N[K];
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, k;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < K; ++i) {
        for (k = 0; k < LENS[i]; ++k) pool[i][k] = (wchar_t)(L'a' + (k % 26));
        pool[i][LENS[i]] = 0;
        wsprintfA(names[i], "%d characters", LENS[i]);
        N[i] = names[i];
        cx[i].s = pool[i];
        cs[i].label = N[i];
        cs[i].bytes = ((size_t)LENS[i] * 2 + 2) * REPS;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }

    printf("  pre-flight (what each row reaches):\n");
    for (i = 0; i < K; ++i) {
        BSTR b = SysAllocString(pool[i]);
        printf("    %-16s SysStringLen %u\n", N[i], b ? (unsigned)SysStringLen(b) : 0u);
        if ((int)SysStringLen(b) != LENS[i]) {
            printf("      ^^ THIS ROW DOES NOT REACH THE PATH ITS NAME PROMISES\n");
            SysFreeString(b);
            return 1;
        }
        SysFreeString(b);
    }

    return wia_bench_compare("oleaut32 SysAllocString  (wia: an AVX2 scan into the OS allocator, "
                             "vs the shipped export)", cs, K, 300);
}
