/* changes/255-rtlfindlongestrunclear/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll export.
 *
 * BOTH OBSERVABLES on every case -- the returned length AND the written *StartingIndex. The index is
 * where the tie-break lives, and a bitmap with a unique longest run cannot test it at all: an
 * implementation that updated its best on ">=" instead of ">" would return the right LENGTH
 * everywhere and the wrong INDEX only when two runs tie. So the corpora are built to tie constantly.
 *
 * THE CORPORA ARE SPLIT BY WHICH BOUNDARY THEY ATTACK, because every hard part of this
 * implementation is a boundary:
 *
 *   1. EXHAUSTIVE over every 16-bit bitmap -- all 65 536 of them, at every declared size 1..16.
 *      Nothing is sampled; the whole space is covered.
 *   2. THE 64-BIT WORD BOUNDARY -- runs planted across it at every offset and every length, which
 *      is where the carry between words lives.
 *   3. THE ODD TRAILING ULONG -- sizes whose ULONG count is odd, so the last word must be read as
 *      32 bits and not 64. Placed against a PAGE_NOACCESS page so a 64-bit read FAULTS.
 *   4. THE SLACK PAST SizeOfBitMap -- every size 1..256 over a buffer that is entirely clear past
 *      it, so any failure to mask reports a longer run.
 *   5. TIES -- many equal-length runs, so the FIRST must win, at every word offset.
 *   6. RANDOMISED at several densities, including the all-clear and all-set extremes.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Longest)(RBM*, PULONG);

ULONG wia_findlongestrunclear(void*, ULONG*);
ULONG ref_findlongestrunclear(void*, ULONG*);

static F_Longest live;
static long fails = 0, cases = 0;

static void one(ULONG* buf, ULONG size, const char* where)
{
    RBM bm;
    ULONG ro = 0xEEEE, rr = 0xEEEE, rl = 0xEEEE;
    ULONG io = 0xEEEE, ir = 0xEEEE, il = 0xEEEE;
    ++cases;
    bm.SizeOfBitMap = size; bm.Buffer = buf;
    ro = wia_findlongestrunclear(&bm, &io);
    rr = ref_findlongestrunclear(&bm, &ir);
    rl = live(&bm, &il);
    if (ro != rr || ro != rl || io != ir || io != il) {
        if (++fails <= 25)
            printf("  MISMATCH [%s] size=%lu  len ours=%lu ref=%lu live=%lu  "
                   "start ours=%lu ref=%lu live=%lu  words=%08lX %08lX\n",
                   where, size, ro, rr, rl, io, ir, il,
                   size ? buf[0] : 0, size > 32 ? buf[1] : 0);
    }
}

static unsigned long long rs = 0x123456789ABCDEFull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    live = (F_Longest)GetProcAddress(h, "RtlFindLongestRunClear");
    if (!live) { printf("RtlFindLongestRunClear not found\n"); return 1; }

    printf("== CORRECTNESS: RtlFindLongestRunClear (ours vs oracle vs LIVE ntdll) ==\n");
    printf("   every case compares the returned LENGTH and the written *StartingIndex\n");

    /* ---- 1. EXHAUSTIVE over every 16-bit bitmap, at every size ---- */
    {
        long before = cases;
        ULONG v, sz;
        ULONG buf[2];
        for (v = 0; v < 65536; ++v) {
            buf[0] = v | 0xFFFF0000u;          /* the upper half set, so slack cannot help */
            buf[1] = 0xFFFFFFFFu;
            for (sz = 1; sz <= 16; ++sz) one(buf, sz, "exhaustive 16-bit");
        }
        printf("  1. EXHAUSTIVE: all 65536 16-bit bitmaps x sizes 1..16: %ld cases\n",
               cases - before);
    }

    /* ---- 2. the 64-bit word boundary ---- */
    {
        long before = cases;
        ULONG buf[8];
        int start, len, i;
        for (start = 40; start <= 90; ++start)
            for (len = 1; len <= 60; ++len) {
                for (i = 0; i < 8; ++i) buf[i] = 0xFFFFFFFFu;
                for (i = start; i < start + len && i < 256; ++i)
                    buf[i >> 5] &= ~(1u << (i & 31));
                one(buf, 256, "64-bit boundary");
            }
        printf("  2. runs across the 64-bit boundary, start 40..90 x length 1..60: %ld cases\n",
               cases - before);
    }

    /* ---- 3. the ODD trailing ULONG, against a guard page ---- */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long before = cases, guard = 0;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  3. guard page SKIPPED\n");
        } else {
            ULONG nw, sz, k;
            /* place the buffer so its LAST valid ULONG ends exactly at the page boundary: a 64-bit
               read of that last word touches PAGE_NOACCESS and raises */
            for (nw = 1; nw <= 65; nw += 2) {                 /* ODD ULONG counts only */
                ULONG* buf = (ULONG*)(base + si.dwPageSize) - nw;
                for (k = 0; k < nw; ++k) buf[k] = (k & 1) ? 0xF0F0F0F0u : 0x0F0F0F0Fu;
                for (sz = (nw - 1) * 32 + 1; sz <= nw * 32; ++sz) {
                    one(buf, sz, "odd trailing ULONG at a guard page");
                    ++guard;
                }
                for (k = 0; k < nw; ++k) buf[k] = 0;          /* all clear: the longest possible */
                one(buf, nw * 32, "odd trailing ULONG, all clear");
                ++guard;
            }
            printf("  3. ODD ULONG counts 1..65 with the buffer against a PAGE_NOACCESS page: "
                   "%ld cases, no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
        (void)before;
    }

    /* ---- 4. the slack past SizeOfBitMap ---- */
    {
        long before = cases;
        ULONG buf[16];
        ULONG sz;
        int i;
        for (i = 0; i < 16; ++i) buf[i] = 0;                  /* EVERYTHING clear */
        for (sz = 1; sz <= 512; ++sz) one(buf, sz, "all clear, slack must not count");
        for (i = 0; i < 16; ++i) buf[i] = 0xFFFFFFFFu;
        for (sz = 1; sz <= 512; ++sz) {
            /* clear only the bits AT and ABOVE sz-4, so part of the run is out of range */
            int b;
            for (i = 0; i < 16; ++i) buf[i] = 0xFFFFFFFFu;
            for (b = (int)sz - 4; b < 512; ++b) if (b >= 0) buf[b >> 5] &= ~(1u << (b & 31));
            one(buf, sz, "a run running off the end");
        }
        printf("  4. the slack past SizeOfBitMap, sizes 1..512 both ways: %ld cases\n",
               cases - before);
    }

    /* ---- 5. TIES: the FIRST equal-length run must win ---- */
    {
        long before = cases;
        ULONG buf[16];
        int gap, len, i, b, n;
        for (gap = 1; gap <= 9; ++gap)
            for (len = 1; len <= 9; ++len) {
                for (i = 0; i < 16; ++i) buf[i] = 0xFFFFFFFFu;
                for (b = 3; b + len <= 500; b += len + gap)
                    for (n = 0; n < len; ++n) buf[(b + n) >> 5] &= ~(1u << ((b + n) & 31));
                one(buf, 512, "many equal runs");
                /* and the same pattern shifted, so ties straddle word boundaries differently */
                for (i = 0; i < 16; ++i) buf[i] = 0xFFFFFFFFu;
                for (b = 60; b + len <= 500; b += len + gap)
                    for (n = 0; n < len; ++n) buf[(b + n) >> 5] &= ~(1u << ((b + n) & 31));
                one(buf, 512, "many equal runs, shifted");
            }
        printf("  5. ties: equal runs at every length 1..9 and gap 1..9: %ld cases\n",
               cases - before);
    }

    /* ---- 6. randomised at several densities ---- */
    {
        long before = cases;
        ULONG buf[64];
        int trial, i, d;
        static const int DENS[6] = { 0, 1, 4, 16, 64, 100 };   /* percent of bits CLEARED */
        for (trial = 0; trial < 60000; ++trial) {
            ULONG sz;
            d = DENS[rnd() % 6];
            for (i = 0; i < 64; ++i) buf[i] = 0xFFFFFFFFu;
            if (d == 100) { for (i = 0; i < 64; ++i) buf[i] = 0; }
            else for (i = 0; i < 2048; ++i)
                     if ((int)(rnd() % 100) < d) buf[i >> 5] &= ~(1u << (i & 31));
            sz = 1 + (rnd() % 2048);
            one(buf, sz, "randomised");
        }
        printf("  6. randomised, 6 densities including all-clear and all-set: %ld cases\n",
               cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (length AND start index exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
