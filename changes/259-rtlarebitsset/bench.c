/* changes/259-rtlarebitsset/bench.c
 *
 * OURS vs the LIVE ntdll!RtlAreBitsSet and ntdll!RtlAreBitsClear.
 *
 * THE ANSWER IS THE SUBJECT. A range check that says NO stops at the first bit that disagrees; one
 * that says YES has examined every bit in the range. They are different amounts of work wearing one
 * name, and the first bitmap survey measured this pair at 1.60 ns and 2.20 ns -- both answering NO,
 * inside the first word -- and filed it as "not a target". So every row below says WHICH ANSWER IT
 * GOT, and the NO rows say WHERE the disagreement is, because a NO that is one word in and a NO that
 * is eight kilobytes in are also different functions:
 *
 *   YES        the whole range is uniform: the expensive case, and the one worth improving
 *   NO at 0    the first word disagrees: the early exit, where a vector loop can only lose
 *   NO late    the disagreement is near the end: almost the whole range is examined anyway
 *
 * AND BOTH ENDS ARE MEASURED UNALIGNED, because the range's first and last words are masked and the
 * middle is not: a row that started and ended on a word boundary would never execute the masks.
 *
 * The short rows are here for the same reason they are in every change in this family: a function
 * whose whole cost is its prologue on a 33-bit bitmap is where a vector implementation goes wrong,
 * and it is measured rather than assumed to be rare.
 */
#include "bench.h"

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef BOOLEAN (NTAPI *F_Are)(RBM*, ULONG, ULONG);

BOOLEAN wia_arebitsset(void*, ULONG, ULONG);
BOOLEAN wia_arebitsclear(void*, ULONG, ULONG);
static F_Are live_set, live_clr;

typedef struct { RBM bm; ULONG start, len; int set; } CTX;

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    return c->set ? (uint64_t)wia_arebitsset(&c->bm, c->start, c->len)
                  : (uint64_t)wia_arebitsclear(&c->bm, c->start, c->len);
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    return c->set ? (uint64_t)live_set(&c->bm, c->start, c->len)
                  : (uint64_t)live_clr(&c->bm, c->start, c->len);
}

#define MAXCASE 24
#define WORDS   2048                       /* 64 Kbit */
static ULONG    pool[MAXCASE][WORDS];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

/* wrong < 0 makes the range uniform (the answer is YES); otherwise that bit disagrees */
static void add(const char* label, ULONG bits, ULONG start, ULONG len, int set, long wrong)
{
    int k = nc, i;
    ULONG* b = pool[k];
    ULONG nw = (bits + 31) / 32;
    for (i = 0; i < (int)nw; ++i) b[i] = set ? 0xFFFFFFFFu : 0u;
    if (wrong >= 0) {
        if (set) b[wrong >> 5] &= ~(1u << (wrong & 31));
        else     b[wrong >> 5] |=  (1u << (wrong & 31));
    }
    ctxs[k].bm.SizeOfBitMap = bits;
    ctxs[k].bm.Buffer = b;
    ctxs[k].start = start; ctxs[k].len = len; ctxs[k].set = set;
    cases[k].label  = label;
    cases[k].bytes  = (size_t)((len + 7) / 8);
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    live_set = (F_Are)GetProcAddress(h, "RtlAreBitsSet");
    live_clr = (F_Are)GetProcAddress(h, "RtlAreBitsClear");
    if (!live_set || !live_clr) { printf("RtlAreBits* not found\n"); return 1; }

    /*   label                                        bits   start    len  set  wrong */
    add("SET 64 Kbit, whole map (YES)",              65536,      0, 65536, 1,    -1);
    add("SET 60000 bits from 0 (YES)",               65536,      0, 60000, 1,    -1);
    add("SET 60000 from 3, UNALIGNED both ends (YES)",65536,     3, 59993, 1,    -1);
    add("SET 8 Kbit (YES)",                           8192,      0,  8192, 1,    -1);
    add("SET 1 Kbit (YES)",                           1024,      0,  1024, 1,    -1);
    add("SET 256 bits (YES)",                          256,      0,   256, 1,    -1);
    add("SET 64 bits (YES)",                            64,      0,    64, 1,    -1);
    add("SET 33 bits (odd ULONG count) (YES)",          33,      0,    33, 1,    -1);
    add("SET 20 bits inside ONE word (YES)",          1024,      5,    20, 1,    -1);
    add("SET 64 Kbit, NO at bit 0",                  65536,      0, 65536, 1,     0);
    add("SET 64 Kbit, NO at bit 40",                 65536,      0, 65536, 1,    40);
    add("SET 64 Kbit, NO at bit 65535 (almost all)", 65536,      0, 65536, 1, 65535);
    add("SET refused: length 0",                     65536,      0,     0, 1,    -1);
    add("SET refused: one bit too long",             65536,      0, 65537, 1,    -1);

    add("CLR 64 Kbit, whole map (YES)",              65536,      0, 65536, 0,    -1);
    add("CLR 60000 from 3, UNALIGNED both ends (YES)",65536,     3, 59993, 0,    -1);
    add("CLR 8 Kbit (YES)",                           8192,      0,  8192, 0,    -1);
    add("CLR 1 Kbit (YES)",                           1024,      0,  1024, 0,    -1);
    add("CLR 64 bits (YES)",                            64,      0,    64, 0,    -1);
    add("CLR 33 bits (odd ULONG count) (YES)",          33,      0,    33, 0,    -1);
    add("CLR 64 Kbit, NO at bit 0",                  65536,      0, 65536, 0,     0);
    add("CLR 64 Kbit, NO at bit 65535 (almost all)", 65536,      0, 65536, 0, 65535);

    printf("== SUBJECTS (what each row actually answers) ==\n");
    printf("  %-44s %8s %7s %7s   %-6s %-6s\n", "case", "bits", "start", "len", "ours", "live");
    for (i = 0; i < nc; ++i) {
        uint64_t ro = op_ours(&ctxs[i]), rl = op_live(&ctxs[i]);
        printf("  %-44s %8lu %7lu %7lu   %-6s %-6s%s\n", cases[i].label,
               ctxs[i].bm.SizeOfBitMap, ctxs[i].start, ctxs[i].len,
               ro ? "YES" : "no", rl ? "YES" : "no",
               (ro == rl) ? "" : "   <== DISAGREE");
        if (ro != rl) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll!RtlAreBitsSet / RtlAreBitsClear", cases, nc, 25);
}
