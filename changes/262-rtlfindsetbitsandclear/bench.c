/* changes/262-rtlfindsetbitsandclear/bench.c
 *
 * OURS vs the LIVE ntdll!RtlFindSetBitsAndClear and ntdll!RtlFindClearBitsAndSet.
 *
 * ------------------------------------------------------------------------------------------------
 * THESE CALLS MUTATE, WHICH MEANS A NAIVE ROW MEASURES A DIFFERENT BITMAP EVERY ITERATION.
 *
 * The harness calls the op tens of thousands of times in a tight loop. A call that consumes a run
 * leaves a different bitmap behind, so iteration two does different work from iteration one and by
 * iteration fifty the subject is exhausted and every call is a full not-found scan. The row would
 * report an average over a subject that changed underneath it, and the two sides would not even be
 * averaging over the same sequence of states.
 *
 * RESTORING THE BUFFER INSIDE THE OP IS NOT THE FIX, and change 142 is the reason this is spelled
 * out: its bench undid an in-place edit with a memcpy of the whole path, which at 16 characters
 * cost as much as the function -- THE RESTORE WAS REPLACING THE MEASUREMENT -- and the row read
 * 0.85x for code that was actually 2.13x. Any restore here lands on both sides equally, which is
 * worse than it sounds: it is a constant added to both, and change 261 measured exactly what a
 * shared constant does to a ratio (it drags it to 1.00x and lets the timer quantisation pick the
 * winner).
 *
 * So every row is built to be SELF-RESTORING, in one of two ways, and neither costs a single
 * instruction of restore:
 *
 *   NOT FOUND -- the call scans the whole bitmap and, by contract, writes NOTHING. It is perfectly
 *       repeatable. This is also the survey's own subject: discovery/ntdll_bitmap2.c measured
 *       1237.00 ns and 410.05 ns on exactly this shape, so the headline rows and the rows that
 *       justified the change are the same rows.
 *
 *   A PAIR THAT IS ITS OWN INVERSE -- over an all-ones bitmap, RtlFindSetBitsAndClear(N, 0) clears
 *       bits 0..N-1, and RtlFindClearBitsAndSet(N, 0) then finds exactly those N clear bits and
 *       sets them again. The bitmap is identical afterwards, both halves do a real search AND a
 *       real mutation, and the op is exactly repeatable. This is how the MUTATION gets measured at
 *       all, including at sizes where the fill is the dominant cost.
 *
 * THE SUBJECT TABLE CHECKS BOTH PROPERTIES rather than asserting them: every not-found row is
 * verified to return 0xFFFFFFFF and to leave the buffer bit-identical, and every pair row is
 * verified to leave the buffer bit-identical after the pair. A row that did not restore itself
 * would still produce a plausible time.
 *
 * ------------------------------------------------------------------------------------------------
 * AND THE SMALL ROWS CALL SIXTEEN TIMES, for the reason change 261 established by measuring it:
 * an empty call through this harness costs 2.32 ns, which is eighty per cent of what a small call
 * measures, so a row that small compares the harness against itself. Rows of 32 words or less are
 * timed as sixteen calls (or sixteen pairs) and their labels say so.
 */
#include "bench.h"

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);

ULONG wia_findsetbitsandclear(void*, ULONG, ULONG);
ULONG wia_findclearbitsandset(void*, ULONG, ULONG);
static F_Find live_fsac, live_fcas;

enum { K_FSAC, K_FCAS, K_PAIR };
typedef struct { RBM bm; ULONG n; ULONG hint; int kind; ULONG reps; } CTX;

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i) {
        if (c->kind == K_FSAC)      acc += wia_findsetbitsandclear(&c->bm, c->n, c->hint);
        else if (c->kind == K_FCAS) acc += wia_findclearbitsandset(&c->bm, c->n, c->hint);
        else {
            acc += wia_findsetbitsandclear(&c->bm, c->n, c->hint);
            acc += wia_findclearbitsandset(&c->bm, c->n, c->hint);
        }
    }
    return acc;
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i) {
        if (c->kind == K_FSAC)      acc += live_fsac(&c->bm, c->n, c->hint);
        else if (c->kind == K_FCAS) acc += live_fcas(&c->bm, c->n, c->hint);
        else {
            acc += live_fsac(&c->bm, c->n, c->hint);
            acc += live_fcas(&c->bm, c->n, c->hint);
        }
    }
    return acc;
}

#define MAXCASE 18
#define WORDS   2048                       /* 64 Kbit */
static ULONG    pool[MAXCASE][WORDS];
static ULONG    gold[MAXCASE][WORDS];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

/* pattern 0 = all ones, otherwise the ULONG value to fill with */
static void add(const char* label, ULONG bits, ULONG pattern, ULONG n, ULONG hint, int kind)
{
    int k = nc, i;
    ULONG nw = (bits + 31) / 32;
    for (i = 0; i < (int)nw; ++i) pool[k][i] = pattern;
    memcpy(gold[k], pool[k], nw * sizeof(ULONG));
    ctxs[k].bm.SizeOfBitMap = bits;
    ctxs[k].bm.Buffer = pool[k];
    ctxs[k].n = n;
    ctxs[k].hint = hint;
    ctxs[k].kind = kind;
    ctxs[k].reps = (nw <= 32) ? 16 : 1;
    cases[k].label  = label;
    cases[k].bytes  = (size_t)nw * 4 * ctxs[k].reps * (kind == K_PAIR ? 2 : 1);
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    live_fsac = (F_Find)GetProcAddress(h, "RtlFindSetBitsAndClear");
    live_fcas = (F_Find)GetProcAddress(h, "RtlFindClearBitsAndSet");
    if (!live_fsac || !live_fcas) { printf("RtlFind*BitsAnd* not found\n"); return 1; }

    /* ---- NOT FOUND: a full scan that writes nothing. The survey's own subject. ---- */
    /*   label                                        bits  pattern       N   hint  kind */
    add("NOTFOUND FSAC 8 Kbit sparse (the survey)",   8192, 0xA5A5A5A5u, 64,  0, K_FSAC);
    add("NOTFOUND FCAS 8 Kbit sparse (the survey)",   8192, 0xA5A5A5A5u, 64,  0, K_FCAS);
    add("NOTFOUND FSAC 64 Kbit sparse",              65536, 0xA5A5A5A5u, 64,  0, K_FSAC);
    add("NOTFOUND FCAS 64 Kbit sparse",              65536, 0xA5A5A5A5u, 64,  0, K_FCAS);
    add("NOTFOUND FSAC 64 Kbit, runs of 4",          65536, 0x0F0F0F0Fu, 17,  0, K_FSAC);
    add("NOTFOUND FSAC 64 Kbit, N=1000",             65536, 0xA5A5A5A5u, 1000, 0, K_FSAC);
    add("NOTFOUND FSAC 1 Kbit sparse (x16)",          1024, 0xA5A5A5A5u, 64,  0, K_FSAC);
    add("NOTFOUND FCAS 1 Kbit sparse (x16)",          1024, 0xA5A5A5A5u, 64,  0, K_FCAS);
    add("NOTFOUND FSAC 256 bits sparse (x16)",         256, 0xA5A5A5A5u, 64,  0, K_FSAC);
    add("NOTFOUND FSAC 64 bits sparse (x16)",           64, 0xA5A5A5A5u, 16,  0, K_FSAC);

    /* ---- FOUND: a pair that is its own inverse, so the mutation is in the measurement ---- */
    add("PAIR 64 Kbit all ones, N=8",                65536, 0xFFFFFFFFu,    8, 0, K_PAIR);
    add("PAIR 64 Kbit all ones, N=64",               65536, 0xFFFFFFFFu,   64, 0, K_PAIR);
    add("PAIR 64 Kbit all ones, N=1024",             65536, 0xFFFFFFFFu, 1024, 0, K_PAIR);
    add("PAIR 64 Kbit all ones, N=30000",            65536, 0xFFFFFFFFu, 30000, 0, K_PAIR);
    add("PAIR 64 Kbit all ones, N=65536 (all)",      65536, 0xFFFFFFFFu, 65536, 0, K_PAIR);
    add("PAIR 1 Kbit all ones, N=8 (x16)",            1024, 0xFFFFFFFFu,    8, 0, K_PAIR);
    add("PAIR 256 bits all ones, N=8 (x16)",           256, 0xFFFFFFFFu,    8, 0, K_PAIR);
    add("PAIR 64 bits all ones, N=8 (x16)",             64, 0xFFFFFFFFu,    8, 0, K_PAIR);

    printf("== SUBJECTS (and the check that each row restores itself) ==\n");
    printf("  %-44s %8s %6s   %-22s %s\n", "case", "bits", "N", "ours", "restores itself");
    for (i = 0; i < nc; ++i) {
        ULONG nw = (ctxs[i].bm.SizeOfBitMap + 31) / 32;
        ULONG ro, rl, r2;
        int self_ok, agree;

        /* ours, from the golden buffer */
        memcpy(pool[i], gold[i], nw * sizeof(ULONG));
        if (ctxs[i].kind == K_FCAS) ro = wia_findclearbitsandset(&ctxs[i].bm, ctxs[i].n, ctxs[i].hint);
        else                        ro = wia_findsetbitsandclear(&ctxs[i].bm, ctxs[i].n, ctxs[i].hint);
        if (ctxs[i].kind == K_PAIR) wia_findclearbitsandset(&ctxs[i].bm, ctxs[i].n, ctxs[i].hint);
        self_ok = (memcmp(pool[i], gold[i], nw * sizeof(ULONG)) == 0);
        r2 = ro;

        /* the live export, from the golden buffer */
        memcpy(pool[i], gold[i], nw * sizeof(ULONG));
        if (ctxs[i].kind == K_FCAS) rl = live_fcas(&ctxs[i].bm, ctxs[i].n, ctxs[i].hint);
        else                        rl = live_fsac(&ctxs[i].bm, ctxs[i].n, ctxs[i].hint);
        if (ctxs[i].kind == K_PAIR) live_fcas(&ctxs[i].bm, ctxs[i].n, ctxs[i].hint);
        agree = (r2 == rl) && (memcmp(pool[i], gold[i], nw * sizeof(ULONG)) == 0) == self_ok;

        memcpy(pool[i], gold[i], nw * sizeof(ULONG));      /* time from the golden state */

        printf("  %-44s %8lu %6lu   ", cases[i].label, ctxs[i].bm.SizeOfBitMap, ctxs[i].n);
        if (ro == 0xFFFFFFFFul) printf("%-22s ", "NOT FOUND (no write)");
        else                    printf("found at %-13lu ", ro);
        printf("%s%s\n", self_ok ? "yes" : "NO  <== the row would drift",
               agree ? "" : "   <== ours and ntdll DISAGREE");
        if (!self_ok || !agree) ++bad;
    }
    if (bad) { printf("\n%d row(s) unusable -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll!RtlFindSetBitsAndClear / RtlFindClearBitsAndSet",
                             cases, nc, 25);
}
