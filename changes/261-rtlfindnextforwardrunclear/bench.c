/* changes/261-rtlfindnextforwardrunclear/bench.c
 *
 * OURS vs the LIVE ntdll!RtlFindNextForwardRunClear and ntdll!RtlFindLastBackwardRunClear.
 *
 * What each row costs is how far it has to walk, so the distance to the answer is the subject and
 * every row states it. A hole in the first word and a hole eight kilobytes away are the same call
 * with a thousand-fold difference in work, and the first bitmap survey's row for this pair happened
 * to be the far one.
 *
 *   far      the only hole is deliberately distant: the scan is the whole cost
 *   none     no hole at all: the scan runs to the end of the bitmap and answers zero
 *   near     the hole is in the first word: nothing to scan, and all that is left is the prologue
 *
 * Both directions get all three, because they are separate code with separate costs, the shipped
 * backward form reads through a 64-bit `bt` and a downward loop, the forward one through a 32-bit
 * loop, and the survey measured them at 0.100 and 0.053 ns/byte on the same bitmap.
 *
 * And the run's length is part of the work, not just finding it: after the first clear bit the
 * function must walk to the end of the run. The `long run` rows make that walk the dominant cost,
 * which is a different loop from the one that found the run.
 *
 * Every row prints what it returned (the length and the start) because a scan that answered
 * from the wrong place would still produce a plausible time.
 *
 * ------------------------------------------------------------------------------------------------
 * The small rows call sixteen times per timed op, and that is not padding. probes/floor.c measures
 * this harness with an op that calls an EMPTY stub:
 *
 *       the loop and an op returning a constant        0.96 ns
 *       ... plus a call to an empty NTAPI stub         2.52 ns
 *       ... in this file's exact op shape              2.32 ns
 *       OURS, 33-bit bitmap                            2.92 ns
 *       ntdll, the same bitmap                         2.96 ns
 *
 * The empty call is eighty per cent of the measurement. What is actually being compared on a
 * two-word bitmap is 0.60 ns against 0.64 ns, and the remaining 2.32 ns is the same constant on
 * both sides, so the ratio is dragged to 1.00x however fast the code is, and the quantisation of
 * a 2.9 ns measurement then decides which side "wins" from run to run. Three genuine structural
 * fixes to the forward scan each moved the long rows and left those two at 1.00x, which is what
 * sent me to measure the floor instead of writing more assembly.
 *
 * So every row of 32 words or less is timed as SIXTEEN calls, with FromIndex walking over seven
 * consecutive values so the compiler cannot hoist the call and the predictor is not fed one single
 * address. Both sides get exactly the same treatment, the seven FromIndex values all produce the
 * same answer on that row's bitmap (they are all outside the run), and the ns column for those
 * rows is the cost of SIXTEEN calls; their labels say so. `bytes` is scaled to match so the GB/s
 * column stays honest.
 */
#include "bench.h"

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Run)(RBM*, ULONG, PULONG);

ULONG wia_findnextforwardrunclear(void*, ULONG, PULONG);
ULONG wia_findlastbackwardrunclear(void*, ULONG, PULONG);
static F_Run live_fwd, live_back;

typedef struct { RBM bm; ULONG from; int backward; ULONG reps; } CTX;

/* FromIndex for repetition i. Forward walks UP from `from`, backward DOWN, seven values either
   way -- every one of them still outside the row's run, so all sixteen calls have one answer. */
#define FROM_AT(c, i) ((c)->backward ? (c)->from - (ULONG)((i) % 7) : (c)->from + (ULONG)((i) % 7))

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    uint64_t acc = 0;
    ULONG i, s;
    for (i = 0; i < c->reps; ++i) {
        ULONG f = FROM_AT(c, i);
        s = 0;
        acc += c->backward ? wia_findlastbackwardrunclear(&c->bm, f, &s)
                           : wia_findnextforwardrunclear(&c->bm, f, &s);
        acc += s;
    }
    return acc;
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    uint64_t acc = 0;
    ULONG i, s;
    for (i = 0; i < c->reps; ++i) {
        ULONG f = FROM_AT(c, i);
        s = 0;
        acc += c->backward ? live_back(&c->bm, f, &s) : live_fwd(&c->bm, f, &s);
        acc += s;
    }
    return acc;
}

#define MAXCASE 20
#define WORDS   2048                       /* 64 Kbit */
static ULONG    pool[MAXCASE][WORDS];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

/* hole_at < 0 means no hole at all */
static void add(const char* label, ULONG bits, ULONG from, int backward, long hole_at, ULONG hole_len)
{
    int k = nc, i;
    ULONG* b = pool[k];
    ULONG nw = (bits + 31) / 32;
    for (i = 0; i < (int)nw; ++i) b[i] = 0xFFFFFFFFu;
    if (hole_at >= 0)
        for (i = 0; i < (int)hole_len; ++i)
            if ((ULONG)hole_at + i < bits) b[((ULONG)hole_at + i) >> 5] &= ~(1u << (((ULONG)hole_at + i) & 31));
    ctxs[k].bm.SizeOfBitMap = bits;
    ctxs[k].bm.Buffer = b;
    ctxs[k].from = from;
    ctxs[k].backward = backward;
    ctxs[k].reps = (nw <= 32) ? 16 : 1;         /* see the note about the floor, at the top */
    cases[k].label  = label;
    cases[k].bytes  = (size_t)nw * 4 * ctxs[k].reps;
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    live_fwd  = (F_Run)GetProcAddress(h, "RtlFindNextForwardRunClear");
    live_back = (F_Run)GetProcAddress(h, "RtlFindLastBackwardRunClear");
    if (!live_fwd || !live_back) { printf("RtlFind*RunClear not found\n"); return 1; }

    /*   label                                      bits   from   back  hole_at  hole_len */
    add("FWD 64 Kbit, hole FAR at 32000",          65536,     1, 0,      32000,  32);
    add("FWD 64 Kbit, hole at the very END",       65536,     1, 0,      65504,  32);
    add("FWD 64 Kbit, NO hole at all",             65536,     1, 0,         -1,   0);
    add("FWD 64 Kbit, hole NEAR at 40",            65536,     1, 0,         40,  32);
    add("FWD 64 Kbit, a LONG run of 30000",        65536,     1, 0,        100, 30000);
    add("FWD 8 Kbit, hole far",                     8192,     1, 0,       4000,  32);
    add("FWD 1 Kbit, hole far  (x16 calls)",        1024,     1, 0,        500,  32);
    add("FWD 256 bits, hole far (x16 calls)",        256,     1, 0,        200,  16);
    add("FWD 64 bits, hole at 40 (x16 calls)",        64,     1, 0,         40,  16);
    add("FWD 33 bits, odd count (x16 calls)",         33,     1, 0,         20,   8);

    add("BACK 64 Kbit, hole FAR at 32000",         65536, 65535, 1,      32000,  32);
    add("BACK 64 Kbit, hole at the very START",    65536, 65535, 1,          0,  32);
    add("BACK 64 Kbit, NO hole at all",            65536, 65535, 1,         -1,   0);
    add("BACK 64 Kbit, hole NEAR at 65400",        65536, 65535, 1,      65400,  32);
    add("BACK 64 Kbit, a LONG run of 30000",       65536, 65535, 1,      30000, 30000);
    add("BACK 8 Kbit, hole far",                    8192,  8191, 1,       1000,  32);
    add("BACK 1 Kbit, hole far  (x16 calls)",       1024,  1023, 1,        100,  32);
    add("BACK 256 bits, hole far (x16 calls)",       256,   255, 1,         20,  16);
    add("BACK 64 bits, hole at 8 (x16 calls)",        64,    63, 1,          8,  16);
    add("BACK 33 bits, odd count (x16 calls)",        33,    32, 1,          4,   8);

    printf("== SUBJECTS (what each row actually finds) ==\n");
    printf("  %-40s %8s %7s   %-16s %-16s\n", "case", "bits", "from", "ours", "live");
    for (i = 0; i < nc; ++i) {
        ULONG so = 0, sl = 0, lo, ll;
        if (ctxs[i].backward) {
            lo = wia_findlastbackwardrunclear(&ctxs[i].bm, ctxs[i].from, &so);
            ll = live_back(&ctxs[i].bm, ctxs[i].from, &sl);
        } else {
            lo = wia_findnextforwardrunclear(&ctxs[i].bm, ctxs[i].from, &so);
            ll = live_fwd(&ctxs[i].bm, ctxs[i].from, &sl);
        }
        printf("  %-40s %8lu %7lu   len=%-5lu at %-6lu len=%-5lu at %-6lu%s\n", cases[i].label,
               ctxs[i].bm.SizeOfBitMap, ctxs[i].from, lo, so, ll, sl,
               (lo == ll && so == sl) ? "" : "   <== DISAGREE");
        if (lo != ll || so != sl) ++bad;

        /* And the seven FromIndex values a repeated row walks over must all give that same answer.
           If one of them landed inside the run the row would be timing a mixture of two different
           amounts of work, so this is checked rather than asserted. */
        if (ctxs[i].reps > 1) {
            ULONG j;
            for (j = 0; j < 7; ++j) {
                ULONG sj = 0, lj, f = FROM_AT(&ctxs[i], j);
                lj = ctxs[i].backward ? wia_findlastbackwardrunclear(&ctxs[i].bm, f, &sj)
                                      : wia_findnextforwardrunclear(&ctxs[i].bm, f, &sj);
                if (lj != lo || sj != so) {
                    printf("      FromIndex %lu answers len=%lu at %lu, not len=%lu at %lu"
                           "   <== the repeated row is not one subject\n", f, lj, sj, lo, so);
                    ++bad;
                }
            }
        }
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll!RtlFindNextForwardRunClear / RtlFindLastBackwardRunClear",
                             cases, nc, 25);
}
