/* changes/258-rtlfindclearruns/bench.c
 *
 * OURS vs the LIVE ntdll!RtlFindClearRuns.
 *
 * THIS FUNCTION HAS TWO COSTS, NOT ONE, and a table that mixed them would be meaningless. With
 * SortByLength FALSE the scan stops the instant the array is full, so the cost is "how far in are
 * the first N runs" and the bitmap's size barely enters into it. With SortByLength TRUE the longest
 * runs cannot be known without seeing all of them, so the cost is the whole bitmap, every time.
 * discovery/ntdll_bitmap.c measured the same call on the same bitmap at 144.83 ns and 14399.10 ns
 * on that one BOOLEAN. So every shape below is measured BOTH WAYS, and the two forms are separate
 * rows rather than an average.
 *
 * THE SHAPES, chosen the same way change 255's were -- a run search's cost depends on the bitmap's
 * shape and not only its size:
 *
 *   all set        -- no clear bits at all. Nothing is ever emitted; the whole map is rejected in
 *                     one compare per 64 bits. UNSORTED does NOT stop early here -- there is
 *                     nothing to fill the array with -- so this is a full scan in both forms.
 *   dense          -- eight runs in 64 Kbit. Unsorted with room for eight has to walk the entire
 *                     bitmap to find its eighth run, so it cannot short-circuit either.
 *   realistic      -- ~200 free extents of varying length, which is what a live bitmap looks like.
 *   sparse         -- 0xA5A5A5A5: a clear run every two bits, SIXTEEN THOUSAND of them. Sorted,
 *                     this is the adversarial shape; unsorted, it is the opposite -- the array
 *                     fills in the first few bytes and the call returns almost immediately.
 *   half-and-half  -- alternating 32-bit blocks of ones and zeros: the longest run inside a word is
 *                     32, which is the worst case for the sorted form's `x &= x >> 1` skip test.
 *   all clear      -- one run covering everything, and the fast path for both forms.
 *
 * AND THE CAPACITY IS PART OF THE SUBJECT, not a detail: SizeOfRunArray = 1 is what
 * RtlFindLongestRunClear passes, and a larger array makes the sorted form's insertion longer and
 * the unsorted form's scan longer. Rows at 1, 8 and 64 are all here.
 *
 * EVERY ROW STATES WHAT IT FOUND before the table -- the number of runs and the first entry ours
 * and the live export agreed on. A run search whose answer is not what the row's name implies would
 * still produce a perfectly plausible time.
 */
#include "bench.h"

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef struct { ULONG StartingIndex; ULONG NumberOfBits; } RUN;
typedef ULONG (NTAPI *F_Runs)(RBM*, RUN*, ULONG, BOOLEAN);

ULONG wia_findclearruns(void*, RUN*, ULONG, BOOLEAN);
static F_Runs live;

typedef struct { RBM bm; ULONG cap; BOOLEAN sorted; RUN arr[128]; } CTX;

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    ULONG n = wia_findclearruns(&c->bm, c->arr, c->cap, c->sorted);
    return (uint64_t)n + c->arr[0].StartingIndex + c->arr[0].NumberOfBits;
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    ULONG n = live(&c->bm, c->arr, c->cap, c->sorted);
    return (uint64_t)n + c->arr[0].StartingIndex + c->arr[0].NumberOfBits;
}

#define MAXCASE 32
#define WORDS   2048                       /* 64 Kbit */
static ULONG    pool[MAXCASE][WORDS];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

static unsigned long long rs = 0x9E3779B97F4A7C15ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

/* shape: 0 all-set, 1 dense, 2 realistic, 3 sparse, 4 half-and-half, 5 all-clear */
static void add(const char* label, int shape, ULONG bits, ULONG cap, int sorted)
{
    int k = nc, i;
    ULONG* b = pool[k];
    ULONG nw = (bits + 31) / 32;
    rs = 0x9E3779B97F4A7C15ull;            /* the same "realistic" map at every size and capacity */
    for (i = 0; i < (int)nw; ++i) {
        switch (shape) {
        case 0:  b[i] = 0xFFFFFFFFu; break;
        case 1:  b[i] = 0xFFFFFFFFu; break;
        case 2:  b[i] = 0xFFFFFFFFu; break;
        case 3:  b[i] = 0xA5A5A5A5u; break;
        case 4:  b[i] = (i & 1) ? 0xFFFFFFFFu : 0x00000000u; break;
        default: b[i] = 0x00000000u; break;
        }
    }
    if (shape == 1) for (i = 0; i < 8 && i * 200 < (int)nw; ++i) b[i * 200] = 0;
    if (shape == 2) {
        for (i = 0; i < 200; ++i) {
            ULONG start = rnd() % (bits > 64 ? bits - 64 : 1);
            ULONG len = 1 + (rnd() % 40);
            ULONG x;
            for (x = start; x < start + len && x < bits; ++x) b[x >> 5] &= ~(1u << (x & 31));
        }
    }
    ctxs[k].bm.SizeOfBitMap = bits;
    ctxs[k].bm.Buffer = b;
    ctxs[k].cap    = cap;
    ctxs[k].sorted = (BOOLEAN)sorted;
    memset(ctxs[k].arr, 0, sizeof ctxs[k].arr);
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
    live = (F_Runs)GetProcAddress(h, "RtlFindClearRuns");
    if (!live) { printf("RtlFindClearRuns not found\n"); return 1; }

    /*   label                                     shape  bits   cap sorted */
    add("64 Kbit all set,       cap 1, SORTED",        0, 65536,  1, 1);
    add("64 Kbit dense,         cap 8, SORTED",        1, 65536,  8, 1);
    add("64 Kbit realistic,     cap 1, SORTED",        2, 65536,  1, 1);
    add("64 Kbit realistic,    cap 16, SORTED",        2, 65536, 16, 1);
    add("64 Kbit realistic,    cap 64, SORTED",        2, 65536, 64, 1);
    add("64 Kbit sparse,        cap 1, SORTED",        3, 65536,  1, 1);
    add("64 Kbit sparse,       cap 64, SORTED",        3, 65536, 64, 1);
    add("64 Kbit half-and-half, cap 8, SORTED",        4, 65536,  8, 1);
    add("64 Kbit all clear,     cap 4, SORTED",        5, 65536,  4, 1);
    add("8 Kbit realistic,     cap 16, SORTED",        2,  8192, 16, 1);
    add("1 Kbit realistic,      cap 8, SORTED",        2,  1024,  8, 1);
    add("256-bit sparse,        cap 8, SORTED",        3,   256,  8, 1);
    add("64-bit sparse,         cap 4, SORTED",        3,    64,  4, 1);
    add("33-bit sparse,         cap 4, SORTED",        3,    33,  4, 1);

    add("64 Kbit all set,       cap 4, UNSORTED",      0, 65536,  4, 0);
    add("64 Kbit dense,         cap 8, UNSORTED",      1, 65536,  8, 0);
    add("64 Kbit realistic,    cap 64, UNSORTED",      2, 65536, 64, 0);
    add("64 Kbit sparse,       cap 64, UNSORTED",      3, 65536, 64, 0);
    add("64 Kbit half-and-half, cap 8, UNSORTED",      4, 65536,  8, 0);
    add("64 Kbit all clear,     cap 4, UNSORTED",      5, 65536,  4, 0);
    add("1 Kbit sparse,        cap 64, UNSORTED",      3,  1024, 64, 0);
    add("256-bit sparse,        cap 8, UNSORTED",      3,   256,  8, 0);
    add("64-bit sparse,         cap 4, UNSORTED",      3,    64,  4, 0);
    add("33-bit sparse,         cap 4, UNSORTED",      3,    33,  4, 0);

    printf("== SUBJECTS (what each row actually returns) ==\n");
    printf("  %-42s %7s  %-18s %-18s\n", "case", "bits", "ours", "live");
    for (i = 0; i < nc; ++i) {
        RUN ao[128], al[128];
        ULONG no, nl;
        memset(ao, 0xEE, sizeof ao); memset(al, 0xEE, sizeof al);
        no = wia_findclearruns(&ctxs[i].bm, ao, ctxs[i].cap, ctxs[i].sorted);
        nl = live(&ctxs[i].bm, al, ctxs[i].cap, ctxs[i].sorted);
        printf("  %-42s %7lu  %2lu run(s) (%lu,%lu)   %2lu run(s) (%lu,%lu)%s\n",
               cases[i].label, ctxs[i].bm.SizeOfBitMap,
               no, no ? ao[0].StartingIndex : 0, no ? ao[0].NumberOfBits : 0,
               nl, nl ? al[0].StartingIndex : 0, nl ? al[0].NumberOfBits : 0,
               (no == nl && memcmp(ao, al, sizeof ao) == 0) ? "" : "   <== DISAGREE");
        if (no != nl || memcmp(ao, al, sizeof ao) != 0) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll!RtlFindClearRuns", cases, nc, 25);
}
