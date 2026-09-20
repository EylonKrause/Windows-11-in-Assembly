/* changes/266-rtliszeromemory/bench.c
 *
 * OURS vs the LIVE ntdll!RtlIsZeroMemory.
 *
 * What this costs is how far it gets before it can answer, and probes/contract.c established that
 * the shipped export STOPS at the first non-zero byte; one megabyte costs 1.55 ns with the
 * non-zero byte first and 51424 ns with it last. So "where the first non-zero byte is" is the
 * subject of every row, and a row set that only measured all-zero buffers would describe a
 * different function from the one callers use.
 *
 *   all zero        the whole buffer is read: the expensive case, and the one the name suggests
 *   byte at 0       nothing is read: the floor for a call, and the case the early exit exists for
 *   byte near the   the scan runs almost to the end; the one that would expose a tail that
 *   end             re-reads or a final vector that is not overlapping
 *
 * The short rows call sixteen times per timed op, for the reason change 261 established by
 * measuring it: an empty call through this harness costs 2.32 ns, which is most of what a 32-byte
 * scan measures. Their labels say so.
 *
 * Every row prints what it answered and it is checked against the live export before anything is
 * timed; a predicate that answered from the wrong place would still produce a plausible time.
 */
#include "bench.h"

typedef BOOLEAN (NTAPI *F_IsZero)(const void*, SIZE_T);

BOOLEAN wia_iszeromemory(const void*, SIZE_T);
static F_IsZero live;

typedef struct { const void* p; SIZE_T n; ULONG reps; } CTX;

static uint64_t op_ours(void* q)
{
    CTX* c = (CTX*)q;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i) acc += wia_iszeromemory(c->p, c->n);
    return acc;
}
static uint64_t op_live(void* q)
{
    CTX* c = (CTX*)q;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i) acc += live(c->p, c->n);
    return acc;
}

#define MAXCASE 20
#define BIG     (1 << 20)
static unsigned char pool[BIG + 64];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

/* the subject buffer is shared, so each row states where its non-zero byte goes and it is put
   there immediately before that row is timed; set_at < 0 means it stays entirely zero */
static long where[MAXCASE];

static void add(const char* label, size_t n, long set_at)
{
    int k = nc;
    ctxs[k].p = pool;
    ctxs[k].n = n;
    /* Repetitions are chosen by how much work the call does, not by how big the buffer is. a row
       whose non-zero byte is at offset 0 answers immediately whatever its length, so it measures
       the harness -- 2.32 ns of empty call, per change 261 -- rather than the code, and it read
       1.00x-1.02x until it was repeated like the genuinely short rows. */
    ctxs[k].reps = (n <= 64 || set_at == 0) ? 16 : 1;
    where[k] = set_at;
    cases[k].label  = label;
    cases[k].bytes  = n * ctxs[k].reps;
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    live = (F_IsZero)GetProcAddress(h, "RtlIsZeroMemory");
    if (!live) { printf("RtlIsZeroMemory not found\n"); return 1; }
    memset(pool, 0, sizeof pool);

    /*   label                                          bytes      first non-zero byte */
    add("1 MB, all zero",                              BIG,   -1);
    add("1 MB, non-zero at the END",                   BIG,   BIG - 1);
    add("1 MB, non-zero at byte 0",                    BIG,   0);
    add("64 KB, all zero",                           65536,   -1);
    add("64 KB, non-zero at the END",                65536,   65535);
    add("64 KB, non-zero at byte 0",                 65536,   0);
    add("64 KB, non-zero at 32000",                  65536,   32000);
    add("4 KB, all zero",                             4096,   -1);
    add("4 KB, non-zero at the END",                  4096,   4095);
    add("1 KB, all zero",                             1024,   -1);
    add("256 bytes, all zero",                         256,   -1);
    add("200 bytes, all zero (not a multiple of 32)",   200,   -1);
    add("128 bytes, all zero",                         128,   -1);
    add("64 bytes, all zero (x16 calls)",               64,   -1);
    add("33 bytes, all zero (x16 calls)",               33,   -1);
    add("32 bytes, all zero (x16 calls)",               32,   -1);
    add("31 bytes, all zero (x16 calls)",               31,   -1);
    add("16 bytes, all zero (x16 calls)",               16,   -1);
    add("8 bytes, all zero (x16 calls)",                 8,   -1);
    add("1 byte, zero (x16 calls)",                      1,   -1);

    printf("== SUBJECTS (what each row actually answers) ==\n");
    printf("  %-46s %9s %10s   %-6s %s\n", "case", "bytes", "1st nonzero", "ours", "live");
    for (i = 0; i < nc; ++i) {
        BOOLEAN ro, rl;
        memset(pool, 0, sizeof pool);
        if (where[i] >= 0) pool[where[i]] = 0x5A;
        ro = wia_iszeromemory(ctxs[i].p, ctxs[i].n);
        rl = live(ctxs[i].p, ctxs[i].n);
        printf("  %-46s %9Iu %10ld   %-6s %-6s%s\n", cases[i].label, ctxs[i].n, where[i],
               ro ? "TRUE" : "false", rl ? "TRUE" : "false",
               (!!ro == !!rl) ? "" : "   <== DISAGREE");
        if (!!ro != !!rl) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }

    /* Each row is timed with its own byte in place, which is why this does not call
       wia_bench_compare: that helper times every row from one prepared state, and here the state
       IS the subject -- where the first non-zero byte sits is the whole difference between a row
       that reads nothing and a row that reads a megabyte. The verdict rule is the harness's own:
       BETTER at 1.03x, WORSE at 0.97x, and a single regressed class parks the change. */
    {
        int r;
        int worst_ok = 1;
        double geo = 0.0;
        printf("\n== BENCH: ntdll!RtlIsZeroMemory ==\n");
        printf("%-46s %11s %11s %9s  %s\n", "size", "ours ns", "system ns", "ratio", "verdict");
        printf("--------------------------------------------------------------------------------\n");
        for (r = 0; r < nc; ++r) {
            volatile uint64_t sink = 0;
            double o, s, ratio;
            memset(pool, 0, sizeof pool);
            if (where[r] >= 0) pool[where[r]] = 0x5A;
            wia_pin(2);
            o = wia_measure(cases[r].ours,   cases[r].ctx, 25, &sink);
            s = wia_measure(cases[r].system, cases[r].ctx, 25, &sink);
            ratio = (o > 0.0) ? s / o : 0.0;
            printf("%-46s %11.2f %11.2f %8.2fx  %s\n", cases[r].label, o, s, ratio,
                   ratio >= 1.03 ? "BETTER" : (ratio <= 0.97 ? "WORSE" : "~tie"));
            if (ratio <= 0.97) worst_ok = 0;
            geo += log(ratio);
        }
        printf("--------------------------------------------------------------------------------\n");
        printf("overall speed ratio (geomean, >1 = ours faster): %.3fx  => %s\n",
               exp(geo / (double)nc), worst_ok ? "LANDS (no size class regressed)" :
               "PARKED (a size class regressed)");
        return worst_ok ? 0 : 1;
    }
}
