/* changes/257-rtlnumberofsetbits/bench.c
 *
 * OURS vs the LIVE ntdll!RtlNumberOfSetBits family.
 *
 * Counting does not care what the bits are -- there is no early exit and no data-dependent branch in
 * either implementation -- so the rows vary the two things that DO matter: the SIZE, which decides
 * whether the vector body runs at all, and the ALIGNMENT of the range, which decides how much of
 * the work falls to the masked partial words at the ends.
 *
 * The density rows are kept anyway, and they are worth keeping precisely BECAUSE they should all
 * cost the same: a row that moved with density would mean one of the two implementations had a
 * data-dependent path nobody had noticed.
 *
 * Every row states what it counted before the table, because a range that quietly refuses returns
 * 0xFFFFFFFF instantly and would otherwise look like a spectacular result.
 */
#include "bench.h"

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Count)(RBM*);
typedef ULONG (NTAPI *F_Range)(RBM*, ULONG, ULONG);

ULONG wia_numberofsetbits(void*);
ULONG wia_numberofclearbits(void*);
ULONG wia_numberofsetbitsinrange(void*, ULONG, ULONG);
ULONG wia_numberofclearbitsinrange(void*, ULONG, ULONG);

static F_Count live_set, live_clr;
static F_Range live_rset, live_rclr;

typedef struct { RBM bm; ULONG st, ln; int kind; } CTX;   /* kind: 0 set,1 clr,2 rset,3 rclr */

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    switch (c->kind) {
    case 0:  return wia_numberofsetbits(&c->bm);
    case 1:  return wia_numberofclearbits(&c->bm);
    case 2:  return wia_numberofsetbitsinrange(&c->bm, c->st, c->ln);
    default: return wia_numberofclearbitsinrange(&c->bm, c->st, c->ln);
    }
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    switch (c->kind) {
    case 0:  return live_set(&c->bm);
    case 1:  return live_clr(&c->bm);
    case 2:  return live_rset(&c->bm, c->st, c->ln);
    default: return live_rclr(&c->bm, c->st, c->ln);
    }
}

#define MAXCASE 20
#define WORDS   2048                       /* 64 Kbit */
static ULONG    pool[MAXCASE][WORDS];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

static unsigned long long rs = 0x8A5CD789635D2DFFull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void add(const char* label, ULONG bits, int density, int kind, ULONG st, ULONG ln)
{
    int k = nc, i;
    ULONG* b = pool[k];
    ULONG nw = (bits + 31) / 32;
    for (i = 0; i < (int)nw; ++i)
        b[i] = (density == 0)   ? 0u
             : (density == 100) ? 0xFFFFFFFFu
             : (density == 50)  ? 0xA5A5A5A5u
                                : (ULONG)rnd();
    ctxs[k].bm.SizeOfBitMap = bits; ctxs[k].bm.Buffer = b;
    ctxs[k].st = st; ctxs[k].ln = ln; ctxs[k].kind = kind;
    cases[k].label  = label;
    cases[k].bytes  = (size_t)((kind >= 2 ? ln : bits) + 7) / 8;
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    live_set  = (F_Count)GetProcAddress(h, "RtlNumberOfSetBits");
    live_clr  = (F_Count)GetProcAddress(h, "RtlNumberOfClearBits");
    live_rset = (F_Range)GetProcAddress(h, "RtlNumberOfSetBitsInRange");
    live_rclr = (F_Range)GetProcAddress(h, "RtlNumberOfClearBitsInRange");
    if (!live_set || !live_clr || !live_rset || !live_rclr) { printf("resolve failed\n"); return 1; }

    /*   label                             bits   dens kind   st     ln */
    add("SET 64 Kbit, half set",          65536,  50, 0,     0,     0);
    add("SET 64 Kbit, all set",           65536, 100, 0,     0,     0);
    add("SET 64 Kbit, all clear",         65536,   0, 0,     0,     0);
    add("SET 64 Kbit, random",            65536,  77, 0,     0,     0);
    add("CLR 64 Kbit, half set",          65536,  50, 1,     0,     0);
    add("SET 8 Kbit",                      8192,  50, 0,     0,     0);
    add("SET 1 Kbit",                      1024,  50, 0,     0,     0);
    add("SET 256 bit",                      256,  50, 0,     0,     0);
    add("SET 64 bit",                        64,  50, 0,     0,     0);
    add("SET 33 bit (odd ULONG count)",      33,  50, 0,     0,     0);
    add("SET 1 bit",                          1,  50, 0,     0,     0);
    add("RANGE set, aligned 0..65536",    65536,  50, 2,     0, 65536);
    add("RANGE set, UNaligned 3..60003",  65536,  50, 2,     3, 60000);
    add("RANGE set, 100..60100",          65536,  50, 2,   100, 60000);
    add("RANGE clr, 100..60100",          65536,  50, 3,   100, 60000);
    add("RANGE set, 8 Kbit 5..8000",       8192,  50, 2,     5,  8000);
    add("RANGE set, 1 Kbit 7..1000",       1024,  50, 2,     7,  1000);
    add("RANGE set, 70 bits 3..73",         256,  50, 2,     3,    70);
    add("RANGE set, 10 bits 5..15",         256,  50, 2,     5,    10);

    printf("== SUBJECTS (what each row actually counts) ==\n");
    printf("  %-34s %8s %7s %7s  %12s %12s\n", "case", "bits", "start", "len", "ours", "live");
    for (i = 0; i < nc; ++i) {
        CTX* c = &ctxs[i];
        uint64_t a = op_ours(c), b = op_live(c);
        printf("  %-34s %8lu %7lu %7lu  %12lld %12lld%s\n", cases[i].label, c->bm.SizeOfBitMap,
               c->st, c->ln, (long long)(long)(ULONG)a, (long long)(long)(ULONG)b,
               (a == b) ? "" : "   <== DISAGREE");
        if (a != b) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }
    printf("  (a range row showing -1 would be one the export REFUSED, which returns instantly and\n"
           "   would otherwise look like a spectacular result)\n");

    return wia_bench_compare("ntdll!RtlNumberOfSetBits family", cases, nc, 25);
}
