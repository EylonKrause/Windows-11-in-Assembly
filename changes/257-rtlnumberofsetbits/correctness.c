/* changes/257-rtlnumberofsetbits/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll exports, for all four of
 * RtlNumberOfSetBits, RtlNumberOfClearBits, RtlNumberOfSetBitsInRange and
 * RtlNumberOfClearBitsInRange.
 *
 * Counting is the easy part; the edges are the whole test. Every corpus below is built to attack
 * one of them:
 *
 *   1. EXHAUSTIVE over every 16-bit bitmap at every declared size. Nothing sampled.
 *   2. every (start, length) over a small bitmap (130 x 130) so the refusal predicate, the
 *      single-word case, and the head/tail split are all covered by construction.
 *   3. The vector seam: lengths that straddle the 32-byte body, so the transition from the vpshufb
 *      loop to the scalar remainder to the masked tail is crossed at every offset.
 *   4. a guard page at the end of the buffer, at odd ulong counts, so a 64-bit read of the final
 *      partial word FAULTS rather than quietly reading four bytes the caller never allocated.
 *   5. THE SLACK past SizeOfBitMap, over an all-ones buffer, where any failure to mask shows up as
 *      a larger count.
 *   6. RANDOMISED at several densities and sizes.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Count)(RBM*);
typedef ULONG (NTAPI *F_Range)(RBM*, ULONG, ULONG);

ULONG wia_numberofsetbits(void*);
ULONG wia_numberofclearbits(void*);
ULONG wia_numberofsetbitsinrange(void*, ULONG, ULONG);
ULONG wia_numberofclearbitsinrange(void*, ULONG, ULONG);
ULONG ref_numberofsetbits(void*);
ULONG ref_numberofclearbits(void*);
ULONG ref_numberofsetbitsinrange(void*, ULONG, ULONG);
ULONG ref_numberofclearbitsinrange(void*, ULONG, ULONG);

static F_Count live_set, live_clr;
static F_Range live_rset, live_rclr;
static long fails = 0, cases = 0;

static void whole(ULONG* buf, ULONG size, const char* where)
{
    RBM bm;
    ULONG a, b, c;
    bm.SizeOfBitMap = size; bm.Buffer = buf;
    ++cases;
    a = wia_numberofsetbits(&bm); b = ref_numberofsetbits(&bm); c = live_set(&bm);
    if (a != b || a != c) {
        if (++fails <= 20) printf("  MISMATCH [%s] SET size=%lu  ours=%lu ref=%lu live=%lu\n",
                                  where, size, a, b, c);
    }
    ++cases;
    a = wia_numberofclearbits(&bm); b = ref_numberofclearbits(&bm); c = live_clr(&bm);
    if (a != b || a != c) {
        if (++fails <= 20) printf("  MISMATCH [%s] CLR size=%lu  ours=%lu ref=%lu live=%lu\n",
                                  where, size, a, b, c);
    }
}

static void range(ULONG* buf, ULONG size, ULONG st, ULONG ln, const char* where)
{
    RBM bm;
    ULONG a, b, c;
    bm.SizeOfBitMap = size; bm.Buffer = buf;
    ++cases;
    a = wia_numberofsetbitsinrange(&bm, st, ln);
    b = ref_numberofsetbitsinrange(&bm, st, ln);
    c = live_rset(&bm, st, ln);
    if (a != b || a != c) {
        if (++fails <= 20) printf("  MISMATCH [%s] RSET size=%lu st=%lu ln=%lu  ours=%lu ref=%lu "
                                  "live=%lu\n", where, size, st, ln, a, b, c);
    }
    ++cases;
    a = wia_numberofclearbitsinrange(&bm, st, ln);
    b = ref_numberofclearbitsinrange(&bm, st, ln);
    c = live_rclr(&bm, st, ln);
    if (a != b || a != c) {
        if (++fails <= 20) printf("  MISMATCH [%s] RCLR size=%lu st=%lu ln=%lu  ours=%lu ref=%lu "
                                  "live=%lu\n", where, size, st, ln, a, b, c);
    }
}

static unsigned long long rs = 0x106689D45497FDB5ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    /* Unbuffered, because this file once died at /O2 before its first line reached the console:
       the entry stubs set their selector in r13 -- a NON-VOLATILE register -- before the framed
       body's prologue saved it, so the caller's r13 was destroyed. Correct at /Od, where the
       compiler spills everything, and an access violation at /O2, where it does not. */
    setvbuf(stdout, NULL, _IONBF, 0);
    live_set  = (F_Count)GetProcAddress(h, "RtlNumberOfSetBits");
    live_clr  = (F_Count)GetProcAddress(h, "RtlNumberOfClearBits");
    live_rset = (F_Range)GetProcAddress(h, "RtlNumberOfSetBitsInRange");
    live_rclr = (F_Range)GetProcAddress(h, "RtlNumberOfClearBitsInRange");
    if (!live_set || !live_clr || !live_rset || !live_rclr) { printf("resolve failed\n"); return 1; }

    printf("== CORRECTNESS: RtlNumberOfSetBits family (ours vs oracle vs LIVE ntdll) ==\n");

    /* ---- 1. EXHAUSTIVE over every 16-bit bitmap ---- */
    {
        long before = cases;
        ULONG v, sz;
        ULONG buf[4];
        for (v = 0; v < 65536; ++v) {
            buf[0] = v | 0xFFFF0000u;          /* the slack is SET: a missing mask shows up */
            buf[1] = 0xFFFFFFFFu; buf[2] = 0xFFFFFFFFu; buf[3] = 0xFFFFFFFFu;
            for (sz = 1; sz <= 16; ++sz) whole(buf, sz, "exhaustive 16-bit");
        }
        printf("  1. EXHAUSTIVE: all 65536 16-bit bitmaps x sizes 1..16, both whole forms: "
               "%ld cases\n", cases - before);
    }

    /* ---- 2. every (start, length) over a small bitmap ---- */
    {
        long before = cases;
        ULONG buf[8];
        ULONG st, ln;
        int i;
        for (i = 0; i < 8; ++i) buf[i] = 0xA5C3F00Fu ^ (ULONG)(i * 0x11111111u);
        for (st = 0; st <= 130; ++st)
            for (ln = 0; ln <= 130; ++ln) range(buf, 128, st, ln, "every start x length");
        printf("  2. every (start, length) 0..130 over a 128-bit bitmap: %ld cases\n",
               cases - before);
    }

    /* ---- 3. the vector seam ---- */
    {
        long before = cases;
        ULONG buf[64];
        ULONG st, ln;
        int i;
        for (i = 0; i < 64; ++i) buf[i] = (ULONG)(rnd());
        for (st = 0; st <= 70; ++st)
            for (ln = 1; ln <= 600; ln += 7)
                if (st + ln <= 2048) range(buf, 2048, st, ln, "vector seam");
        for (ln = 1; ln <= 2048; ++ln) range(buf, 2048, 0, ln, "vector seam from 0");
        printf("  3. the seam between the VPSHUFB body, the scalar remainder and the masked tail: "
               "%ld cases\n", cases - before);
    }

    /* ---- 4. a guard page, at ODD ULONG counts ---- */
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
            ULONG nw, sz, k, st, ln;
            for (nw = 1; nw <= 41; ++nw) {                  /* both odd and even ULONG counts */
                ULONG* buf = (ULONG*)(base + si.dwPageSize) - nw;
                for (k = 0; k < nw; ++k) buf[k] = 0xF0F0A5A5u;
                for (sz = (nw - 1) * 32 + 1; sz <= nw * 32; sz += 5) {
                    whole(buf, sz, "guard page");
                    ++guard;
                    for (st = 0; st < sz; st += 13)
                        for (ln = 1; st + ln <= sz; ln += 11) {
                            range(buf, sz, st, ln, "guard page range");
                            ++guard;
                        }
                }
            }
            printf("  4. a PAGE_NOACCESS page at the end of the buffer, ULONG counts 1..41: "
                   "%ld cases, no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* ---- 5. the slack past SizeOfBitMap ---- */
    {
        long before = cases;
        ULONG buf[64];
        ULONG sz;
        int i;
        for (i = 0; i < 64; ++i) buf[i] = 0xFFFFFFFFu;
        for (sz = 1; sz <= 2048; ++sz) whole(buf, sz, "all ones, slack must not count");
        printf("  5. an all-ones buffer at every size 1..2048: %ld cases\n", cases - before);
    }

    /* ---- 6. randomised ---- */
    {
        long before = cases;
        ULONG buf[256];
        int trial, i, d;
        static const int DENS[5] = { 0, 7, 50, 93, 100 };
        for (trial = 0; trial < 40000; ++trial) {
            ULONG sz, st, ln;
            d = DENS[rnd() % 5];
            for (i = 0; i < 256; ++i) buf[i] = 0u;
            if (d == 100) { for (i = 0; i < 256; ++i) buf[i] = 0xFFFFFFFFu; }
            else for (i = 0; i < 8192; ++i)
                     if ((int)(rnd() % 100) < d) buf[i >> 5] |= (1u << (i & 31));
            sz = 1 + (rnd() % 8192);
            whole(buf, sz, "randomised");
            st = rnd() % (sz + 8);
            ln = rnd() % (sz + 8);
            range(buf, sz, st, ln, "randomised range");
        }
        printf("  6. randomised, 5 densities, sizes to 8192: %ld cases\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf(fails ? "CORRECTNESS: FAILED\n" : "CORRECTNESS: PASS (exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
