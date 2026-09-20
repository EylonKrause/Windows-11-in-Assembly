/* changes/256-rtlfindsetbits/bench.c
 *
 * OURS vs the LIVE ntdll!RtlFindSetBits and ntdll!RtlFindClearBits.
 *
 * Both exports are measured separately even though one implementation serves both, because they
 * are five times apart on the same failing full scan -- 0.132 ns/byte against 0.026 in
 * discovery/ntdll_bitmap.c. A single "bitmap search" row would average that away and hide which of
 * the two the work actually helps. (That gap belongs to the SUBJECT and not to one export being
 * worse: probes/topbit.c shows the two timings SWAP when the pattern is rotated one bit, because
 * both skip loops are driven by the sign bit and one of the two exports inverts the word. Which
 * makes measuring them separately more important, not less -- a row is timing a particular path.)
 *
 * And the rows say whether the search succeeds, because the two cases have nothing in common. a
 * search that fails examines every bit; a search that succeeds in the first word examines one. The
 * survey's headline row is a FAILURE -- a request for 64 consecutive set bits in a bitmap whose
 * longest set run is two -- and it is the expensive case, so it is kept and labelled rather than
 * quietly replaced by a cheaper one.
 *
 * The hint gets its own rows. The search wraps: [hint, size) and then from the beginning. a run
 * that sits just BEFORE a high hint is therefore the worst case for a correct implementation --
 * it scans to the end, finds nothing, and scans again -- and that row is measured rather than
 * assumed to be rare.
 */
#include "bench.h"

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);

ULONG wia_findsetbits(void*, ULONG, ULONG);
ULONG wia_findclearbits(void*, ULONG, ULONG);
static F_Find live_set, live_clr;

typedef struct { RBM bm; ULONG n, hint; int set; } CTX;

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    return c->set ? wia_findsetbits(&c->bm, c->n, c->hint)
                  : wia_findclearbits(&c->bm, c->n, c->hint);
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    return c->set ? live_set(&c->bm, c->n, c->hint) : live_clr(&c->bm, c->n, c->hint);
}

#define MAXCASE 20
#define WORDS   2048                       /* 64 Kbit */
static ULONG    pool[MAXCASE][WORDS];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

static unsigned long long rs = 0x4D595DF4D0F33173ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

/* shape: 0 sparse (0xA5A5A5A5), 1 all clear, 2 all set, 3 realistic (mostly set, ~200 holes) */
static void add(const char* label, int shape, ULONG bits, ULONG n, ULONG hint, int set,
                long plant, ULONG plantlen)
{
    int k = nc, i;
    ULONG* b = pool[k];
    ULONG nw = (bits + 31) / 32;
    for (i = 0; i < (int)nw; ++i)
        b[i] = (shape == 0) ? 0xA5A5A5A5u : (shape == 1) ? 0u : 0xFFFFFFFFu;
    if (shape == 3)
        for (i = 0; i < 200; ++i) {
            ULONG st = rnd() % (bits - 64), ln = 1 + (rnd() % 40), x;
            for (x = st; x < st + ln && x < bits; ++x) b[x >> 5] &= ~(1u << (x & 31));
        }
    if (plant >= 0) {
        ULONG x;
        for (x = (ULONG)plant; x < (ULONG)plant + plantlen && x < bits; ++x) {
            if (set) b[x >> 5] |=  (1u << (x & 31));
            else     b[x >> 5] &= ~(1u << (x & 31));
        }
    }
    ctxs[k].bm.SizeOfBitMap = bits; ctxs[k].bm.Buffer = b;
    ctxs[k].n = n; ctxs[k].hint = hint; ctxs[k].set = set;
    cases[k].label  = label;
    cases[k].bytes  = (size_t)nw * 4;
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    live_set = (F_Find)GetProcAddress(h, "RtlFindSetBits");
    live_clr = (F_Find)GetProcAddress(h, "RtlFindClearBits");
    if (!live_set || !live_clr) { printf("resolve failed\n"); return 1; }

    /*   label                                  shape   bits     n   hint  set  plant  len */
    add("SET 64, sparse -- NOT FOUND",              0, 65536,   64,    0, 1,    -1, 0);
    add("SET 64, found at 59999",                   0, 65536,   64,    0, 1, 60000, 64);
    add("SET 64, found at 100",                     0, 65536,   64,    0, 1,   100, 64);
    add("SET 8, sparse -- NOT FOUND",               0, 65536,    8,    0, 1,    -1, 0);
    add("SET 1, sparse -- found at once",           0, 65536,    1,    0, 1,    -1, 0);
    add("SET 64, all set -- found at 0",            2, 65536,   64,    0, 1,    -1, 0);
    add("SET 64, hint 60000, run at 100 (WRAPS)",   0, 65536,   64, 60000, 1,   100, 64);
    add("SET 64, realistic -- found at 0",          3, 65536,   64,    0, 1,    -1, 0);
    add("SET 64, 8 Kbit sparse -- NOT FOUND",       0,  8192,   64,    0, 1,    -1, 0);
    add("SET 64, 1 Kbit sparse -- NOT FOUND",       0,  1024,   64,    0, 1,    -1, 0);
    add("CLR 64, sparse -- NOT FOUND",              0, 65536,   64,    0, 0,    -1, 0);
    add("CLR 64, found at 60000",                   0, 65536,   64,    0, 0, 60000, 64);
    add("CLR 8, sparse -- NOT FOUND",               0, 65536,    8,    0, 0,    -1, 0);
    add("CLR 64, all clear -- found at 0",          1, 65536,   64,    0, 0,    -1, 0);
    add("CLR 64, hint 60000, hole at 99 (WRAPS)",   0, 65536,   64, 60000, 0,   100, 64);
    add("CLR 64, realistic -- found at 1267",       3, 65536,   64,    0, 0,    -1, 0);
    add("CLR 64, 8 Kbit sparse -- NOT FOUND",       0,  8192,   64,    0, 0,    -1, 0);
    add("SET 1000, all set -- found at 0",          2, 65536, 1000,    0, 1,    -1, 0);

    printf("== SUBJECTS (what each row actually finds) ==\n");
    printf("  %-40s %7s %6s %7s  %10s %10s\n", "case", "bits", "n", "hint", "ours", "live");
    for (i = 0; i < nc; ++i) {
        CTX* c = &ctxs[i];
        ULONG ro = c->set ? wia_findsetbits(&c->bm, c->n, c->hint)
                          : wia_findclearbits(&c->bm, c->n, c->hint);
        ULONG rl = c->set ? live_set(&c->bm, c->n, c->hint) : live_clr(&c->bm, c->n, c->hint);
        printf("  %-40s %7lu %6lu %7lu  %10s %10s%s\n", cases[i].label, c->bm.SizeOfBitMap,
               c->n, c->hint,
               ro == 0xFFFFFFFFu ? "NOT FOUND" : "", rl == 0xFFFFFFFFu ? "NOT FOUND" : "",
               (ro == rl) ? "" : "   <== DISAGREE");
        if (ro != rl) ++bad;
        if (ro != 0xFFFFFFFFu) printf("      %*s found at %lu / %lu\n", 40, "", ro, rl);
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll!RtlFindSetBits / RtlFindClearBits", cases, nc, 25);
}
