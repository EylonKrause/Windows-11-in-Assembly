/* changes/255-rtlfindlongestrunclear/bench.c
 *
 * OURS vs the LIVE ntdll!RtlFindLongestRunClear.
 *
 * The subject matters more here than in any other benchmark in this project, because the cost of a
 * run search depends on the bitmap's SHAPE and not only its size, and the two are easy to confuse.
 * discovery/ntdll_bitmap.c established that the shipped code's cost is NOT per-run, eight runs
 * cost 8423 ns and two hundred cost 8689, so it is a fixed per-bit scan. Ours is not: it is
 * per-word with a per-word inner loop bounded by the longest run INSIDE that word. So the rows below
 * span the whole range of shapes deliberately, and the one this implementation is worst at is
 * included rather than omitted:
 *
 *   all set; no clear bits at all. Every word is rejected by one CMP against -1.
 *   dense; a real allocation bitmap that is nearly full.
 *   realistic, ~200 free extents of varying length, which is what a live bitmap looks like.
 *   sparse, 0xA5A5A5A5: a clear run every two bits, SIXTEEN THOUSAND of them. This is the
 *                     adversarial shape, and the one where a per-run implementation would lose.
 *   half-and-half, alternating 32-bit blocks of ones and zeros: the longest run INSIDE a word is
 *                     32, so the inner `x &= x >> 1` loop runs 33 times for every word it examines.
 *                     This is the worst case for the approach and it is measured.
 *   all clear; one run covering everything; every word takes the all-zero fast path.
 *
 * Every row states what it found before the table, the length and the start index ours and the
 * live export agreed on. A run search whose answer is not what the row's name implies would still
 * produce a plausible time.
 */
#include "bench.h"

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Longest)(RBM*, PULONG);

ULONG wia_findlongestrunclear(void*, ULONG*);
static F_Longest live;

typedef struct { RBM bm; } CTX;

static uint64_t op_ours(void* p)
{
    ULONG ix = 0;
    return (uint64_t)wia_findlongestrunclear(&((CTX*)p)->bm, &ix) + ix;
}
static uint64_t op_live(void* p)
{
    ULONG ix = 0;
    return (uint64_t)live(&((CTX*)p)->bm, &ix) + ix;
}

#define MAXCASE 16
#define WORDS   2048                       /* 64 Kbit */
static ULONG    pool[MAXCASE][WORDS];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

static unsigned long long rs = 0x9E3779B97F4A7C15ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

/* shape: 0 all-set, 1 dense, 2 realistic, 3 sparse, 4 half-and-half, 5 all-clear */
static void add(const char* label, int shape, ULONG bits)
{
    int k = nc, i;
    ULONG* b = pool[k];
    ULONG nw = (bits + 31) / 32;
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
    live = (F_Longest)GetProcAddress(h, "RtlFindLongestRunClear");
    if (!live) { printf("RtlFindLongestRunClear not found\n"); return 1; }

    /*   label                             shape  bits */
    add("64 Kbit all set (no clear bits)",     0, 65536);
    add("64 Kbit dense (8 runs)",              1, 65536);
    add("64 Kbit realistic (~200 runs)",       2, 65536);
    add("64 Kbit sparse (16K runs)",           3, 65536);
    add("64 Kbit half-and-half (worst case)",  4, 65536);
    add("64 Kbit all clear (one run)",         5, 65536);
    add("8 Kbit realistic",                    2,  8192);
    add("8 Kbit sparse",                       3,  8192);
    add("1 Kbit realistic",                    2,  1024);
    add("1 Kbit sparse",                       3,  1024);
    add("256-bit sparse",                      3,   256);
    add("64-bit sparse",                       3,    64);
    add("33-bit sparse (odd ULONG count)",     3,    33);

    printf("== SUBJECTS (what each row actually finds) ==\n");
    printf("  %-38s %8s  %10s %10s\n", "case", "bits", "ours", "live");
    for (i = 0; i < nc; ++i) {
        ULONG io = 0, il = 0, ro, rl;
        ro = wia_findlongestrunclear(&ctxs[i].bm, &io);
        rl = live(&ctxs[i].bm, &il);
        printf("  %-38s %8lu  len=%lu@%lu  len=%lu@%lu%s\n", cases[i].label,
               ctxs[i].bm.SizeOfBitMap, ro, io, rl, il,
               (ro == rl && io == il) ? "" : "   <== DISAGREE");
        if (ro != rl || io != il) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll!RtlFindLongestRunClear", cases, nc, 25);
}
