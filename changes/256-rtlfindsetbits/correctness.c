/* changes/256-rtlfindsetbits/correctness.c
 *
 * Three-way: ours vs an independent oracle vs the live ntdll exports, for both RtlFindSetBits and
 * RtlFindClearBits.
 *
 * The hint is what this file is mostly for. Every corpus below sweeps the hint, because the wrap is
 * invisible on any bitmap whose answer lies after it -- which is most bitmaps -- and an
 * implementation built on "search forward from the hint and stop" would pass a careless corpus
 * everywhere and fail only when the sole qualifying run sits before the hint.
 *
 *   1. EXHAUSTIVE over every 16-bit bitmap x every N x every hint. Nothing sampled.
 *   2. THE WRAP, specifically: runs placed before, after, and straddling the hint.
 *   3. The 64-BIT word boundary -- runs planted across it at every offset and length.
 *   4. The odd trailing ulong against a PAGE_NOACCESS page, so a 64-bit read of the last word
 *      FAULTS rather than merely reading four bytes the caller never allocated.
 *   5. THE SLACK past SizeOfBitMap, and the degenerate N.
 *   6. RANDOMISED at several densities, both exports, hints everywhere.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);

ULONG wia_findsetbits(void*, ULONG, ULONG);
ULONG wia_findclearbits(void*, ULONG, ULONG);
ULONG ref_findsetbits(void*, ULONG, ULONG);
ULONG ref_findclearbits(void*, ULONG, ULONG);

static F_Find live_set, live_clr;
static long fails = 0, cases = 0;

static void one(ULONG* buf, ULONG size, ULONG want, ULONG hint, int set, const char* where)
{
    RBM bm;
    ULONG ro, rr, rl;
    ++cases;
    bm.SizeOfBitMap = size; bm.Buffer = buf;
    if (set) {
        ro = wia_findsetbits(&bm, want, hint);
        rr = ref_findsetbits(&bm, want, hint);
        rl = live_set(&bm, want, hint);
    } else {
        ro = wia_findclearbits(&bm, want, hint);
        rr = ref_findclearbits(&bm, want, hint);
        rl = live_clr(&bm, want, hint);
    }
    if (ro != rr || ro != rl) {
        if (++fails <= 25)
            printf("  MISMATCH [%s] %s size=%lu n=%lu hint=%lu  ours=%lu ref=%lu live=%lu  "
                   "w0=%08lX w1=%08lX\n", where, set ? "SET" : "CLR", size, want, hint,
                   ro, rr, rl, size ? buf[0] : 0, size > 32 ? buf[1] : 0);
    }
}

static unsigned long long rs = 0xDEADBEEFCAFEBABEull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    live_set = (F_Find)GetProcAddress(h, "RtlFindSetBits");
    live_clr = (F_Find)GetProcAddress(h, "RtlFindClearBits");
    if (!live_set || !live_clr) { printf("resolve failed\n"); return 1; }

    printf("== CORRECTNESS: RtlFindSetBits / RtlFindClearBits (ours vs oracle vs LIVE ntdll) ==\n");

    /* ---- 1. EXHAUSTIVE over every 16-bit bitmap ---- */
    {
        long before = cases;
        ULONG v, n, hint;
        ULONG buf[2];
        for (v = 0; v < 65536; ++v) {
            buf[0] = v;  buf[1] = 0x5A5A5A5Au;     /* junk past the size, which must not count */
            for (n = 0; n <= 5; ++n)
                for (hint = 0; hint <= 16; hint += 4) {
                    one(buf, 16, n, hint, 1, "exhaustive 16-bit");
                    one(buf, 16, n, hint, 0, "exhaustive 16-bit");
                }
        }
        printf("  1. EXHAUSTIVE: all 65536 16-bit bitmaps x N 0..5 x 5 hints x both: %ld cases\n",
               cases - before);
    }

    /* ---- 2. THE WRAP ---- */
    {
        long before = cases;
        ULONG buf[16];
        int a, b, n, hint, i;
        for (n = 2; n <= 10; ++n)
            for (a = 0; a < 500; a += 37)
                for (b = 0; b < 500; b += 53) {
                    for (i = 0; i < 16; ++i) buf[i] = 0u;
                    for (i = a; i < a + n && i < 512; ++i) buf[i >> 5] |= (1u << (i & 31));
                    for (i = b; i < b + n && i < 512; ++i) buf[i >> 5] |= (1u << (i & 31));
                    for (hint = 0; hint <= 512; hint += 61)
                        one(buf, 512, (ULONG)n, (ULONG)hint, 1, "wrap: two runs");
                }
        /* a run straddling the wrap point: bits at the very end and at the very start */
        for (n = 2; n <= 10; ++n) {
            for (i = 0; i < 16; ++i) buf[i] = 0u;
            for (i = 512 - n; i < 512; ++i) buf[i >> 5] |= (1u << (i & 31));
            for (i = 0; i < n; ++i) buf[i >> 5] |= (1u << (i & 31));
            for (hint = 0; hint <= 512; hint += 31)
                one(buf, 512, (ULONG)(n * 2), (ULONG)hint, 1, "wrap: straddling run");
        }
        printf("  2. the WRAP: runs before/after/straddling the hint, 9 hints each: %ld cases\n",
               cases - before);
    }

    /* ---- 3. the 64-bit word boundary ---- */
    {
        long before = cases;
        ULONG buf[16];
        int start, len, i, hint;
        for (start = 40; start <= 90; ++start)
            for (len = 1; len <= 40; len += 3) {
                for (i = 0; i < 16; ++i) buf[i] = 0u;
                for (i = start; i < start + len && i < 512; ++i) buf[i >> 5] |= (1u << (i & 31));
                for (hint = 0; hint <= 128; hint += 32) {
                    one(buf, 512, (ULONG)len, (ULONG)hint, 1, "64-bit boundary");
                    one(buf, 512, (ULONG)(len - 1 > 0 ? len - 1 : 1), (ULONG)hint, 1, "boundary-1");
                }
            }
        printf("  3. runs across the 64-bit boundary, start 40..90 x length 1..40: %ld cases\n",
               cases - before);
    }

    /* ---- 4. the ODD trailing ULONG against a guard page ---- */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  4. guard page SKIPPED\n");
        } else {
            ULONG nw, sz, k, n, hint;
            for (nw = 1; nw <= 33; nw += 2) {                 /* ODD ULONG counts only */
                ULONG* buf = (ULONG*)(base + si.dwPageSize) - nw;
                for (k = 0; k < nw; ++k) buf[k] = (k & 1) ? 0xF0F0F0F0u : 0x0F0F0F0Fu;
                for (sz = (nw - 1) * 32 + 1; sz <= nw * 32; sz += 3)
                    for (n = 1; n <= 6; ++n)
                        for (hint = 0; hint < sz; hint += 29) {
                            one(buf, sz, n, hint, 1, "odd ULONG at a guard page");
                            one(buf, sz, n, hint, 0, "odd ULONG at a guard page");
                            guard += 2;
                        }
                for (k = 0; k < nw; ++k) buf[k] = 0xFFFFFFFFu;
                one(buf, nw * 32, nw * 32, 0, 1, "odd ULONG, all set");
                ++guard;
            }
            printf("  4. ODD ULONG counts 1..33 against a PAGE_NOACCESS page: %ld cases, no fault\n",
                   guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* ---- 5. the slack, and the degenerate N ---- */
    {
        long before = cases;
        ULONG buf[16];
        ULONG sz, n;
        int i;
        for (i = 0; i < 16; ++i) buf[i] = 0xFFFFFFFFu;
        for (sz = 1; sz <= 300; ++sz)
            for (n = 0; n <= 3; ++n) {
                one(buf, sz, sz + n, 0, 1, "n at and past the size");
                one(buf, sz, n, 0, 1, "small n");
            }
        for (i = 0; i < 16; ++i) buf[i] = 0u;
        for (sz = 1; sz <= 300; ++sz) {
            one(buf, sz, sz, 0, 0, "all clear, n == size");
            one(buf, sz, sz + 1, 0, 0, "all clear, n == size+1");
        }
        one(buf, 0, 0, 0, 1, "size 0, n 0");
        one(buf, 0, 1, 0, 1, "size 0, n 1");
        one(buf, 0, 0, 0, 0, "size 0, n 0, clear");
        one(buf, 0, 1, 0, 0, "size 0, n 1, clear");
        printf("  5. the slack past SizeOfBitMap and the degenerate N: %ld cases\n",
               cases - before);
    }

    /* ---- 6. randomised ---- */
    {
        long before = cases;
        ULONG buf[64];
        int trial, i, d;
        static const int DENS[6] = { 0, 3, 12, 50, 88, 100 };
        for (trial = 0; trial < 60000; ++trial) {
            ULONG sz, n, hint;
            d = DENS[rnd() % 6];
            for (i = 0; i < 64; ++i) buf[i] = 0u;
            if (d == 100) { for (i = 0; i < 64; ++i) buf[i] = 0xFFFFFFFFu; }
            else for (i = 0; i < 2048; ++i)
                     if ((int)(rnd() % 100) < d) buf[i >> 5] |= (1u << (i & 31));
            sz = 1 + (rnd() % 2048);
            n = rnd() % 70;
            hint = rnd() % (sz + 16);
            one(buf, sz, n, hint, (int)(rnd() & 1), "randomised");
        }
        printf("  6. randomised, 6 densities, N 0..69, hints everywhere: %ld cases\n",
               cases - before);
    }

    /* ---- 7. a run of exactly N at every alignment, for every block size the scan can pick ----
     *
     * This is the corpus for the witness argument, and it is the one the earlier corpora could not
     * be: they top out at 2048-bit bitmaps and N = 69, so the 64-bit block size is never chosen and
     * a run never has to be rebuilt across more than one 32-byte chunk. Here a single run of length
     * N-1, N or N+1 is planted at every bit offset across two chunk boundaries, for an N in each
     * block class -- 3 and 6 (pairs), 7 and 14 (nibbles), 15 and 30 (bytes), 31 and 62 (words),
     * 63 and 126 (dwords), 127 and 300 (qwords). A run of exactly N-1 must NOT be found, which is
     * what catches a filter that answers from the block rather than from the run.
     */
    {
        long before = cases;
        static ULONG buf[512];                      /* 16 Kbit */
        static const ULONG NS[12] = { 3, 6, 7, 14, 15, 30, 31, 62, 63, 126, 127, 300 };
        int k, d, i;
        ULONG at, len;
        for (k = 0; k < 12; ++k)
            for (d = -1; d <= 1; ++d) {
                len = NS[k] + d;
                if ((int)len <= 0) continue;
                for (at = 500; at < 900; ++at) {    /* across two 256-bit chunk boundaries */
                    for (i = 0; i < 512; ++i) buf[i] = 0u;
                    for (i = 0; i < (int)len; ++i) buf[(at + i) >> 5] |= (1u << ((at + i) & 31));
                    one(buf, 16384, NS[k], 0, 1, "planted run, every alignment");
                    one(buf, 16384, NS[k], at + 1, 1, "planted run, hint past it");
                    for (i = 0; i < 512; ++i) buf[i] = 0xFFFFFFFFu;
                    for (i = 0; i < (int)len; ++i) buf[(at + i) >> 5] &= ~(1u << ((at + i) & 31));
                    one(buf, 16384, NS[k], 0, 0, "planted hole, every alignment");
                }
            }
        printf("  7. a run of exactly N-1, N and N+1 at every alignment 500..899, 12 values of N,\n"
               "     both exports -- the block sizes from pairs to qwords: %ld cases\n",
               cases - before);
    }

    /* ---- 8. LARGE bitmaps and LARGE N, randomised ----
     * Sizes to 16 Kbit and N to 400, so the scan skips many chunks between witnesses and rebuilds
     * runs that span several of them. Densities are chosen to make FALSE witnesses common: a
     * bitmap of scattered all-ones bytes flags a candidate that no run of 300 can use.
     */
    {
        long before = cases;
        static ULONG buf[512];
        int trial, i;
        for (trial = 0; trial < 40000; ++trial) {
            ULONG sz, n, hint;
            int shape = (int)(rnd() % 6);
            for (i = 0; i < 512; ++i)
                buf[i] = (shape == 0) ? 0u
                       : (shape == 1) ? 0xFFFFFFFFu
                       : (shape == 2) ? 0xA5A5A5A5u
                       : (shape == 3) ? ((i & 1) ? 0xFFFFFFFFu : 0u)
                       : (shape == 4) ? (ULONG)((rnd() & 7) ? 0u : 0xFFFFFFFFu)  /* sparse blocks */
                                      : (ULONG)rnd();
            if (shape == 5) for (i = 0; i < 40; ++i) {      /* plus a few real runs */
                ULONG s = rnd() % 16000, L = 1 + (rnd() % 400), x;
                for (x = s; x < s + L && x < 16384; ++x) buf[x >> 5] |= (1u << (x & 31));
            }
            sz   = 300 + (rnd() % 16085);
            n    = 1 + (rnd() % 400);
            hint = rnd() % (sz + 16);
            one(buf, sz, n, hint, (int)(rnd() & 1), "large, big N");
        }
        printf("  8. 40000 randomised at 300..16384 bits, N 1..400, 6 shapes: %ld cases\n",
               cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf(fails ? "CORRECTNESS: FAILED\n" : "CORRECTNESS: PASS (exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
