/* changes/256-rtlfindsetbits/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll exports, for BOTH RtlFindSetBits and
 * RtlFindClearBits.
 *
 * THE HINT IS WHAT THIS FILE IS MOSTLY FOR. Every corpus below sweeps the hint, because the wrap is
 * invisible on any bitmap whose answer lies after it -- which is most bitmaps -- and an
 * implementation built on "search forward from the hint and stop" would pass a careless corpus
 * everywhere and fail only when the sole qualifying run sits before the hint.
 *
 *   1. EXHAUSTIVE over every 16-bit bitmap x every N x every hint. Nothing sampled.
 *   2. THE WRAP, specifically: runs placed before, after, and straddling the hint.
 *   3. THE 64-BIT WORD BOUNDARY -- runs planted across it at every offset and length.
 *   4. THE ODD TRAILING ULONG against a PAGE_NOACCESS page, so a 64-bit read of the last word
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

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf(fails ? "CORRECTNESS: FAILED\n" : "CORRECTNESS: PASS (exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
