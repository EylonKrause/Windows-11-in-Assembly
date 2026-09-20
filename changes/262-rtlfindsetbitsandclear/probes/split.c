/* changes/262-rtlfindsetbitsandclear/probes/split.c
 *
 * Where does the general path's time go?
 *
 * Three rows of eighteen sit just under the gate, 256 bits at 0.97x, 1 Kbit at 0.96x, and the
 * N=1024 mutation at 0.90x, and there are two completely different explanations available:
 *
 *   (a) change 256's SEARCH is itself no faster than the shipped code at these sizes, in which case
 *       nothing this change does to its own wrapper can fix the row;
 *   (b) the search is comfortably faster and the wrapper, a frame, a call, a return and the
 *       parameter shuffle around it, is eating the margin, in which case the call has to go.
 *
 * Guessing between those two is how time gets spent on the wrong code, so this measures them
 * apart, on the exact subjects the failing rows use:
 *
 *   1. ntdll!RtlFindSetBitsAndClear, what has to be beaten
 *   2. ntdll!RtlFindSetBits, the same search WITHOUT the mutation, shipped
 *   3. wia_findsetbits (change 256), our search alone, no wrapper at all
 *   4. wia_findsetbitsandclear (262), our search plus this change's wrapper
 *
 * (4) minus (3) Is the wrapper, measured rather than estimated. (1) minus (2) is what the shipped
 * code pays for the same privilege, which is the fair thing to compare it against.
 *
 * What it found, and what it corrected. The wrapper is a flat 0.79 ns at every size, and change
 * 256's search has a fixed cost of about 7 ns that is nearly constant from 128 bits to 1 Kbit --
 * so below 512 bits the search merely TIES the shipped code and the wrapper turns a tie into a
 * loss. That is (a), not (b).
 *
 * The last section then did the same thing to the PAIR rows, and it is the reason this probe has a
 * read-only column at all: the first write-up of this change asserted that the cliff between N=64
 * and N=256 was the MUTATION, a store-forwarding stall between the fill and the next call's
 * vector loads, and credited the search with none of it. Timing the SEARCH ALONE at the same N
 * shows it stepping 2.43 -> 5.03 ns across exactly that boundary, which is more than half of the
 * cliff and belongs to change 256. The hypothesis about the remaining ~2.1 ns may still be right;
 * it is now written down as a hypothesis.
 */
#include "../../../harness/bench.h"

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);

ULONG wia_findsetbitsandclear(void*, ULONG, ULONG);
ULONG wia_findsetbits(void*, ULONG, ULONG);

static F_Find live_fsac, live_fsb;
static RBM bm;
static ULONG n_want;

#define WORDS 2048
static ULONG buf[WORDS];

#define REPS 16
static uint64_t op_live_mut(void* p)
{ uint64_t a = 0; int i; (void)p; for (i = 0; i < REPS; ++i) a += live_fsac(&bm, n_want, 0); return a; }
static uint64_t op_live_pure(void* p)
{ uint64_t a = 0; int i; (void)p; for (i = 0; i < REPS; ++i) a += live_fsb(&bm, n_want, 0); return a; }
static uint64_t op_ours_pure(void* p)
{ uint64_t a = 0; int i; (void)p; for (i = 0; i < REPS; ++i) a += wia_findsetbits(&bm, n_want, 0); return a; }
static uint64_t op_ours_mut(void* p)
{ uint64_t a = 0; int i; (void)p; for (i = 0; i < REPS; ++i) a += wia_findsetbitsandclear(&bm, n_want, 0); return a; }

/* the self-inverting pairs, and the same two searches with no mutation at all */
ULONG wia_findclearbitsandset(void*, ULONG, ULONG);
ULONG wia_findclearbits(void*, ULONG, ULONG);
static F_Find live_fcas, live_fcb;
static uint64_t op_live_pair(void* p)
{ uint64_t a = 0; int i; (void)p;
  for (i = 0; i < REPS; ++i) { a += live_fsac(&bm, n_want, 0); a += live_fcas(&bm, n_want, 0); } return a; }
static uint64_t op_ours_pair(void* p)
{ uint64_t a = 0; int i; (void)p;
  for (i = 0; i < REPS; ++i) { a += wia_findsetbitsandclear(&bm, n_want, 0);
                               a += wia_findclearbitsandset(&bm, n_want, 0); } return a; }
static uint64_t op_live_pure2(void* p)
{ uint64_t a = 0; int i; (void)p;
  for (i = 0; i < REPS; ++i) { a += live_fsb(&bm, n_want, 0); a += live_fcb(&bm, n_want, 0); } return a; }
static uint64_t op_ours_pure2(void* p)
{ uint64_t a = 0; int i; (void)p;
  for (i = 0; i < REPS; ++i) { a += wia_findsetbits(&bm, n_want, 0);
                               a += wia_findclearbits(&bm, n_want, 0); } return a; }

static void row_pat(const char* label, ULONG bits, ULONG n, ULONG pat)
{
    volatile uint64_t sink = 0;
    double lm, lp, op, om;
    ULONG i;
    for (i = 0; i < (bits + 31) / 32; ++i) buf[i] = pat;
    bm.SizeOfBitMap = bits; bm.Buffer = buf; n_want = n;

    lm = wia_measure(op_live_mut,  NULL, 25, &sink) / REPS;
    lp = wia_measure(op_live_pure, NULL, 25, &sink) / REPS;
    op = wia_measure(op_ours_pure, NULL, 25, &sink) / REPS;
    om = wia_measure(op_ours_mut,  NULL, 25, &sink) / REPS;

    printf("  %-22s %9.2f %9.2f %9.2f %9.2f   %8.2f %8.2f\n",
           label, lm, lp, op, om, lm - lp, om - op);
}

/* NOT FOUND: a full scan that writes nothing */
static void row(const char* label, ULONG bits, ULONG n) { row_pat(label, bits, n, 0xA5A5A5A5u); }

/* Found at bit zero: the search is trivial, so the row is almost entirely the mutation, which is
   the other thing that could be wrong. Note this one is NOT repeatable: each call consumes N bits,
   so it is only meaningful as a first-call comparison, and it is printed apart from the rest. */
static void row_found(const char* label, ULONG bits, ULONG n)
{
    volatile uint64_t sink = 0;
    double lp, op;
    ULONG i;
    bm.SizeOfBitMap = bits; bm.Buffer = buf; n_want = n;
    for (i = 0; i < (bits + 31) / 32; ++i) buf[i] = 0xFFFFFFFFu;
    lp = wia_measure(op_live_pure, NULL, 25, &sink) / REPS;    /* read-only: repeatable */
    for (i = 0; i < (bits + 31) / 32; ++i) buf[i] = 0xFFFFFFFFu;
    op = wia_measure(op_ours_pure, NULL, 25, &sink) / REPS;
    printf("  %-22s %9s %9.2f %9.2f %9s   %8s %8s\n", label, "-", lp, op, "-", "-", "-");
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    live_fsac = (F_Find)GetProcAddress(h, "RtlFindSetBitsAndClear");
    live_fsb  = (F_Find)GetProcAddress(h, "RtlFindSetBits");
    live_fcas = (F_Find)GetProcAddress(h, "RtlFindClearBitsAndSet");
    live_fcb  = (F_Find)GetProcAddress(h, "RtlFindClearBits");
    if (!live_fsac || !live_fsb || !live_fcas || !live_fcb) { printf("resolve failed\n"); return 1; }
    wia_pin(2);

    printf("== WHERE THE GENERAL PATH SPENDS ITS TIME (ns per call, NOT FOUND so nothing is written) ==\n");
    printf("  %-22s %9s %9s %9s %9s   %8s %8s\n",
           "subject", "ntdll&C", "ntdll", "ours256", "ours262", "ntdllWr", "oursWr");
    row("64 bits, N=16",     64,   16);
    row("128 bits, N=16",    128,  16);
    row("256 bits, N=64",    256,  64);
    row("512 bits, N=64",    512,  64);
    row("1 Kbit, N=64",      1024, 64);
    row("2 Kbit, N=64",      2048, 64);
    row("8 Kbit, N=64",      8192, 64);
    printf("\n  ntdllWr = what the SHIPPED code pays for the mutation wrapper (and-clear minus plain)\n");
    printf("  oursWr  = what OUR wrapper costs on top of change 256's search\n");

    printf("\n== AND THE SEARCH ALONE WHEN IT FINDS AT BIT ZERO (all ones, read-only) ==\n");
    printf("  the PAIR rows of bench.c are almost entirely this plus the mutation, so if the\n");
    printf("  search is already behind here, no amount of work on the fill will save the row\n");
    printf("  %-22s %9s %9s %9s %9s   %8s %8s\n",
           "subject", "", "ntdll", "ours256", "", "", "");
    row_found("64 Kbit ones, N=8",     65536, 8);
    row_found("64 Kbit ones, N=64",    65536, 64);
    row_found("64 Kbit ones, N=128",   65536, 128);
    row_found("64 Kbit ones, N=192",   65536, 192);
    row_found("64 Kbit ones, N=256",   65536, 256);
    row_found("64 Kbit ones, N=512",   65536, 512);
    row_found("64 Kbit ones, N=1024",  65536, 1024);
    row_found("64 Kbit ones, N=30000", 65536, 30000);

    /* ---- The fill, isolated ----------------------------------------------------------------
       A mutating call cannot be timed twice on the same bitmap, but a PAIR can: over an all-ones
       bitmap, and-clear followed by clear-and-set returns it to exactly what it was. Timing that
       self-inverting pair against the SAME pair of READ-ONLY searches -- which do the identical
       amount of searching and no writing -- leaves the two mutations, and nothing else. */
    {
        volatile uint64_t sink = 0;
        static const ULONG NS[8] = { 8, 64, 128, 192, 256, 512, 1024, 30000 };
        int k;
        /* The read-only baseline is not a valid subtrahend and the columns that used it are gone:
           RtlFindClearBits over an ALL-ONES bitmap finds nothing and scans all 64 Kbit, so the
           "read-only pair" does hundreds of ns more searching than the mutating pair, and the
           difference came out NEGATIVE. What is printed is what can honestly be compared: the two
           self-inverting pairs against each other, beside the SEARCH alone at the same N from the
           table above -- which is what separates the search from the fill. */
        printf("\n== THE SELF-INVERTING PAIR ACROSS N (the only repeatable way to time a mutation) ==\n");
        printf("  %-16s %10s %10s %10s\n", "N", "ntdll pair", "ours pair", "ratio");
        for (k = 0; k < 8; ++k) {
            double lpair, opair;
            ULONG i;
            bm.SizeOfBitMap = 65536; bm.Buffer = buf; n_want = NS[k];
            for (i = 0; i < 2048; ++i) buf[i] = 0xFFFFFFFFu;
            lpair = wia_measure(op_live_pair, NULL, 25, &sink) / REPS;
            for (i = 0; i < 2048; ++i) buf[i] = 0xFFFFFFFFu;
            opair = wia_measure(op_ours_pair, NULL, 25, &sink) / REPS;
            printf("  %-16lu %10.2f %10.2f %9.2fx\n", NS[k], lpair, opair, lpair / opair);
        }
    }
    return 0;
}
