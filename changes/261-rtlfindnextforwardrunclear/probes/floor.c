/* changes/261-rtlfindnextforwardrunclear/probes/floor.c
 *
 * WHAT DOES AN EMPTY CALL COST IN THIS HARNESS?
 *
 * The two smallest FORWARD rows of bench.c sit at 1.00x-1.01x and will not move. Three separate
 * structural fixes -- the slack mask hoisted out of both scans, the out-pointer spill removed, the
 * end scan replaced by change 258's carry strip so a one-word run is answered with no second scan
 * at all -- each made the big rows faster and left those two exactly where they were, ours 3.14 ns
 * against ntdll's 3.16 ns. That is not a code problem, and this probe is how that was established
 * instead of assumed.
 *
 * It measures, through the SAME wia_measure path bench.c uses:
 *
 *   1. an op that returns a constant           -- the loop, the indirect call, the XOR
 *   2. an op that calls an empty NTAPI stub    -- ... plus a call and return
 *   3. an op shaped exactly like bench.c's     -- ... plus the branch and the argument loads
 *   4. ours on a 33-bit bitmap
 *   5. the live export on the same bitmap
 *
 * If (3) is already close to (4) and (5), then the smallest rows are measuring the HARNESS, both
 * functions are underneath its floor, and 1.00x is the honest answer for them rather than a
 * regression to chase.
 */
#include "../../../harness/bench.h"

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Run)(RBM*, ULONG, PULONG);

ULONG wia_findnextforwardrunclear(void*, ULONG, PULONG);
static F_Run live_fwd;

static ULONG NTAPI empty_stub(RBM* b, ULONG f, PULONG s) { (void)b; (void)s; return f; }
static F_Run stub = empty_stub;

typedef struct { RBM bm; ULONG from; int backward; } CTX;
static CTX c;

static uint64_t op_const(void* p) { (void)p; return 1; }
static uint64_t op_stub(void* p)  { CTX* x = (CTX*)p; ULONG s = 0; return stub(&x->bm, x->from, &s) + s; }
static uint64_t op_shape(void* p)
{
    CTX* x = (CTX*)p;
    ULONG s = 0;
    ULONG n = x->backward ? stub(&x->bm, x->from, &s) : stub(&x->bm, x->from, &s);
    return (uint64_t)n + s;
}
static uint64_t op_ours(void* p)
{
    CTX* x = (CTX*)p;
    ULONG s = 0;
    return (uint64_t)wia_findnextforwardrunclear(&x->bm, x->from, &s) + s;
}
static uint64_t op_live(void* p)
{
    CTX* x = (CTX*)p;
    ULONG s = 0;
    return (uint64_t)live_fwd(&x->bm, x->from, &s) + s;
}

int main(void)
{
    static ULONG buf[2];
    volatile uint64_t sink = 0;
    double a, b, d, e, f;
    live_fwd = (F_Run)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlFindNextForwardRunClear");
    if (!live_fwd) { printf("resolve failed\n"); return 1; }

    buf[0] = 0xF00FFFFFu; buf[1] = 0xFFFFFFFFu;   /* bits 20..27 clear: bench.c's 33-bit row */
    c.bm.SizeOfBitMap = 33; c.bm.Buffer = buf; c.from = 1; c.backward = 0;
    wia_pin(2);

    a = wia_measure(op_const, &c, 25, &sink);
    b = wia_measure(op_stub,  &c, 25, &sink);
    d = wia_measure(op_shape, &c, 25, &sink);
    e = wia_measure(op_ours,  &c, 25, &sink);
    f = wia_measure(op_live,  &c, 25, &sink);

    printf("== THE HARNESS FLOOR, measured the way bench.c measures ==\n");
    printf("  %-44s %8.2f ns\n", "1. the loop and an op returning a constant", a);
    printf("  %-44s %8.2f ns\n", "2. ... plus a call to an EMPTY NTAPI stub", b);
    printf("  %-44s %8.2f ns\n", "3. ... in bench.c's exact op shape", d);
    printf("  %-44s %8.2f ns\n", "4. OURS, 33-bit bitmap, hole at 20", e);
    printf("  %-44s %8.2f ns\n", "5. ntdll, the same bitmap", f);
    printf("\n  the empty call is %.0f%% of ours and %.0f%% of ntdll's\n", 100.0 * d / e, 100.0 * d / f);
    printf("  what is left to compare: ours %.2f ns of work, ntdll %.2f ns\n", e - d, f - d);
    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
