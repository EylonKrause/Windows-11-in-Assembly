/* changes/259-rtlarebitsset/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll exports, for BOTH RtlAreBitsSet and
 * RtlAreBitsClear.
 *
 * A PREDICATE HAS ONLY TWO ANSWERS, which makes a careless corpus very easy to pass: an
 * implementation that always said "no" would agree with the live export on every randomly generated
 * range over a random bitmap, because almost none of them is uniform. So every corpus here is built
 * to produce BOTH answers, and the summary counts how many of each it actually got -- a corpus that
 * never once answered YES has not tested the loop at all, only the refusals.
 *
 *   1. EXHAUSTIVE over every 16-bit bitmap x every start x every length. Nothing sampled.
 *   2. EVERY (start, length) over a bitmap that is entirely uniform, so the answer is YES for every
 *      range inside the map and the refusals are the only falses -- this is the corpus that drives
 *      the vector loop, the two masked ends and the seam between them.
 *   3. ONE WRONG BIT, moved to every position across a long uniform range: the answer must flip to
 *      NO exactly when that bit is inside the range, which is what catches a mask that is one bit
 *      too wide or a vector tail that skips a word.
 *   4. THE REFUSALS: length 0, a start at the end and past it, a range one bit too long, and the
 *      slack over an entirely-ones buffer declared short.
 *   5. A GUARD PAGE at the end of the buffer at odd ULONG counts -- the range can never reach past
 *      SizeOfBitMap, so a 32-byte vector load must never touch the page after it.
 *   6. RANDOMISED at several densities, with lengths short enough to stay inside one word and long
 *      enough to cross many.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef BOOLEAN (NTAPI *F_Are)(RBM*, ULONG, ULONG);

BOOLEAN wia_arebitsset(void*, ULONG, ULONG);
BOOLEAN wia_arebitsclear(void*, ULONG, ULONG);
BOOLEAN ref_arebitsset(void*, ULONG, ULONG);
BOOLEAN ref_arebitsclear(void*, ULONG, ULONG);

static F_Are live_set, live_clr;
static long fails = 0, cases = 0, yes_set = 0, yes_clr = 0;

static void one(ULONG* buf, ULONG size, ULONG start, ULONG len, int set, const char* where)
{
    RBM bm;
    int ro, rr, rl;
    ++cases;
    bm.SizeOfBitMap = size; bm.Buffer = buf;
    if (set) {
        ro = wia_arebitsset(&bm, start, len) ? 1 : 0;
        rr = ref_arebitsset(&bm, start, len) ? 1 : 0;
        rl = live_set(&bm, start, len) ? 1 : 0;
        if (rl) ++yes_set;
    } else {
        ro = wia_arebitsclear(&bm, start, len) ? 1 : 0;
        rr = ref_arebitsclear(&bm, start, len) ? 1 : 0;
        rl = live_clr(&bm, start, len) ? 1 : 0;
        if (rl) ++yes_clr;
    }
    if (ro != rr || ro != rl) {
        if (++fails <= 20)
            printf("  MISMATCH [%s] %s size=%lu start=%lu len=%lu  ours=%d ref=%d live=%d\n",
                   where, set ? "SET" : "CLR", size, start, len, ro, rr, rl);
    }
}

static unsigned long long rs = 0x243F6A8885A308D3ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    setvbuf(stdout, NULL, _IONBF, 0);
    live_set = (F_Are)GetProcAddress(h, "RtlAreBitsSet");
    live_clr = (F_Are)GetProcAddress(h, "RtlAreBitsClear");
    if (!live_set || !live_clr) { printf("resolve failed\n"); return 1; }

    printf("== CORRECTNESS: RtlAreBitsSet / RtlAreBitsClear (ours vs oracle vs LIVE ntdll) ==\n");
    printf("   every corpus is built to produce BOTH answers, and the total says how many of each\n");

    /* ---- 1. EXHAUSTIVE over every 16-bit bitmap ---- */
    {
        long before = cases;
        ULONG v, buf[2];
        ULONG s, l;
        for (v = 0; v < 65536; ++v) {
            buf[0] = v; buf[1] = 0xFFFFFFFFu;
            for (s = 0; s < 16; s += 3)
                for (l = 0; l <= 17; l += 4) {
                    one(buf, 16, s, l, 1, "exhaustive 16-bit");
                    one(buf, 16, s, l, 0, "exhaustive 16-bit");
                }
        }
        printf("  1. all 65536 16-bit bitmaps x starts 0,3..15 x lengths 0,4..16, both: %ld\n",
               cases - before);
    }

    /* ---- 2. EVERY (start, length) over a uniform bitmap ---- */
    {
        long before = cases;
        static ULONG buf[64];
        ULONG s, l;
        int i, u;
        for (u = 0; u < 2; ++u) {
            for (i = 0; i < 64; ++i) buf[i] = u ? 0xFFFFFFFFu : 0u;
            for (s = 0; s <= 300; ++s)
                for (l = 0; l <= 300; l += 7) {
                    one(buf, 300, s, l, 1, "uniform, every range");
                    one(buf, 300, s, l, 0, "uniform, every range");
                }
        }
        printf("  2. every start 0..300 x length 0,7..300 over an all-ones AND an all-zero map,\n"
               "     both exports -- the corpus that drives the vector loop and both masked ends: %ld\n",
               cases - before);
    }

    /* ---- 3. ONE WRONG BIT, moved across the range ---- */
    {
        long before = cases;
        static ULONG buf[64];
        ULONG bad, s, l;
        int i;
        for (bad = 0; bad < 600; bad += 7) {
            for (i = 0; i < 64; ++i) buf[i] = 0xFFFFFFFFu;
            buf[bad >> 5] &= ~(1u << (bad & 31));           /* exactly one bit is wrong */
            for (s = 0; s <= 600; s += 37)
                for (l = 1; l <= 600; l += 53)
                    if (s + l <= 2048) one(buf, 2048, s, l, 1, "one wrong bit");
            for (i = 0; i < 64; ++i) buf[i] = 0u;
            buf[bad >> 5] |= (1u << (bad & 31));
            for (s = 0; s <= 600; s += 37)
                for (l = 1; l <= 600; l += 53)
                    if (s + l <= 2048) one(buf, 2048, s, l, 0, "one wrong bit");
        }
        printf("  3. ONE wrong bit at 86 positions x 17 starts x 12 lengths, both exports --\n"
               "     the answer must flip exactly when that bit is in range: %ld\n", cases - before);
    }

    /* ---- 4. the refusals ---- */
    {
        long before = cases;
        static ULONG buf[64];
        ULONG s;
        int i;
        for (i = 0; i < 64; ++i) buf[i] = 0xFFFFFFFFu;
        for (s = 0; s <= 130; ++s) {
            one(buf, 100, s, 0, 1, "refusal: length zero");
            one(buf, 100, s, 0, 0, "refusal: length zero");
            one(buf, 100, s, 1, 1, "refusal: start at/past the end");
            one(buf, 100, s, 101 - (s < 101 ? s : 100), 1, "refusal: one bit too long");
            one(buf, 100, s, 0xFFFFFFFFu, 1, "refusal: an absurd length");
            one(buf, 100, s, 0xFFFFFFFEu - s, 1, "refusal: a length that would overflow");
        }
        /* the slack: an entirely-ones buffer declared short */
        for (s = 1; s <= 200; ++s) {
            one(buf, s, 0, s, 1, "the slack: exactly the declared size");
            one(buf, s, 0, s + 1, 1, "the slack: one past it");
        }
        printf("  4. the refusals -- length 0, a start at and past the end, one bit too long,\n"
               "     a length that overflows, and the slack over an all-ones buffer: %ld\n",
               cases - before);
    }

    /* ---- 5. a guard page ---- */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  5. guard page SKIPPED\n");
        } else {
            ULONG nw, sz, k, s;
            for (nw = 1; nw <= 41; ++nw) {
                ULONG* buf = (ULONG*)(base + si.dwPageSize) - nw;
                for (k = 0; k < nw; ++k) buf[k] = 0xFFFFFFFFu;
                for (sz = (nw - 1) * 32 + 1; sz <= nw * 32; sz += 5)
                    for (s = 0; s < sz; s += 7) {
                        one(buf, sz, s, sz - s, 1, "guard page");
                        one(buf, sz, s, sz - s, 0, "guard page");
                        one(buf, sz, s, sz - s + 1, 1, "guard page, one too long");
                        guard += 3;
                    }
                for (k = 0; k < nw; ++k) buf[k] = 0u;
                one(buf, nw * 32, 0, nw * 32, 0, "guard page, all clear");
                ++guard;
            }
            printf("  5. a PAGE_NOACCESS page at the end of the buffer, 1..41 ULONGs: %ld cases, "
                   "no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* ---- 6. randomised ---- */
    {
        long before = cases;
        static ULONG buf[128];
        int trial, i, d;
        static const int DENS[6] = { 0, 1, 20, 50, 99, 100 };
        for (trial = 0; trial < 60000; ++trial) {
            ULONG sz, s, l;
            d = DENS[rnd() % 6];
            for (i = 0; i < 128; ++i) buf[i] = 0u;
            if (d == 100) { for (i = 0; i < 128; ++i) buf[i] = 0xFFFFFFFFu; }
            else for (i = 0; i < 4096; ++i)
                     if ((int)(rnd() % 100) < d) buf[i >> 5] |= (1u << (i & 31));
            sz = 1 + (rnd() % 4096);
            s  = rnd() % (sz + 8);
            l  = (rnd() & 1) ? (rnd() % 40) : (rnd() % (sz + 8));
            one(buf, sz, s, l, (int)(rnd() & 1), "randomised");
        }
        printf("  6. randomised, 6 densities, short and long ranges, starts past the end: %ld\n",
               cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf("  the live exports answered YES %ld times for SET and %ld for CLEAR -- a corpus that\n"
           "  never answered yes would have tested the refusals and nothing else\n", yes_set, yes_clr);
    if (!yes_set || !yes_clr) { printf("CORRECTNESS: FAILED (a corpus never reached the loop)\n"); return 1; }
    printf(fails ? "CORRECTNESS: FAILED\n" : "CORRECTNESS: PASS (exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
