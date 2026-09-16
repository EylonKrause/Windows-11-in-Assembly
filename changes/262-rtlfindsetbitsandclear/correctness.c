/* changes/262-rtlfindsetbitsandclear/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll exports, for both
 * RtlFindSetBitsAndClear and RtlFindClearBitsAndSet.
 *
 * THE RETURN VALUE IS THE SMALLER HALF OF WHAT HAS TO MATCH. These functions MUTATE the bitmap, so
 * every case is run THREE TIMES ON THREE SEPARATE COPIES of the same input, and all three resulting
 * BUFFERS are compared word for word along with the three answers. An implementation that returned
 * the right index and cleared one bit too many -- or cleared the right bits and also touched a word
 * at the far end of the bitmap -- would pass any test that only looked at the return value, and
 * would corrupt a caller's allocator silently.
 *
 * THE THREE ARMS ARE COUNTED AND THE RUN FAILS IF ANY IS EMPTY. "Found", "not found" and
 * NumberToFind = 0 are three completely different paths through both implementations -- only the
 * first writes anything at all -- and a corpus that never reached one of them would have proved
 * nothing about it.
 *
 *   1. EXHAUSTIVE over every 16-bit bitmap x N x hint, both exports. Every arrangement of runs,
 *      every N that can and cannot be satisfied, and both sides of the wrap.
 *   2. ONE PLANTED RUN at every position and length, asked with every N around its length -- the
 *      "exactly N bits, not the whole run" rule is the one this change adds, and this is where it
 *      lives.
 *   3. REPEATED CALLS on ONE bitmap until it is exhausted: each call must consume exactly what it
 *      claims, so the sequence of answers is itself a check that the mutation is exact.
 *   4. THE WRAP: the only run behind the hint, so the answer is the wrapped one -- and it must
 *      still mutate.
 *   5. A GUARD PAGE at the end of the buffer: the mutation writes, so an overrun here is a fault
 *      rather than a wrong answer.
 *   6. RANDOMISED at several densities, N and hint everywhere including past the end.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);

ULONG wia_findsetbitsandclear(void*, ULONG, ULONG);
ULONG wia_findclearbitsandset(void*, ULONG, ULONG);
ULONG ref_findsetbitsandclear(void*, ULONG, ULONG);
ULONG ref_findclearbitsandset(void*, ULONG, ULONG);
ULONG ref_findsetbitsandclear_null_expect(void);

static F_Find live_fsac, live_fcas;
static long fails = 0, cases = 0, n_found = 0, n_none = 0, n_zero = 0;

#define MAXW 256
static ULONG c_ours[MAXW], c_ref[MAXW], c_live[MAXW];

/* One case, run on three copies of the same input. `nw` is how many words of the buffer are real
   and therefore have to agree afterwards. */
static void one(const ULONG* src, ULONG nw, ULONG size, ULONG n, ULONG hint, int clearside,
                const char* where)
{
    RBM a, b, c;
    ULONG ro, rr, rl;
    ++cases;
    memcpy(c_ours, src, nw * sizeof(ULONG));
    memcpy(c_ref,  src, nw * sizeof(ULONG));
    memcpy(c_live, src, nw * sizeof(ULONG));
    a.SizeOfBitMap = size; a.Buffer = c_ours;
    b.SizeOfBitMap = size; b.Buffer = c_ref;
    c.SizeOfBitMap = size; c.Buffer = c_live;
    if (clearside) {
        ro = wia_findclearbitsandset(&a, n, hint);
        rr = ref_findclearbitsandset(&b, n, hint);
        rl = live_fcas(&c, n, hint);
    } else {
        ro = wia_findsetbitsandclear(&a, n, hint);
        rr = ref_findsetbitsandclear(&b, n, hint);
        rl = live_fsac(&c, n, hint);
    }
    if (n == 0)                      ++n_zero;
    else if (rl == 0xFFFFFFFFul)     ++n_none;
    else                             ++n_found;

    if (ro != rl || rr != rl ||
        memcmp(c_ours, c_live, nw * sizeof(ULONG)) != 0 ||
        memcmp(c_ref,  c_live, nw * sizeof(ULONG)) != 0) {
        if (++fails <= 20) {
            ULONG k;
            printf("  MISMATCH [%s] %s size=%lu N=%lu hint=%lu  ours=%lu ref=%lu live=%lu",
                   where, clearside ? "CLEAR&SET" : "SET&CLEAR", size, n, hint, ro, rr, rl);
            for (k = 0; k < nw; ++k)
                if (c_ours[k] != c_live[k] || c_ref[k] != c_live[k]) {
                    printf("   first differing word [%lu]: ours=%08lX ref=%08lX live=%08lX",
                           k, c_ours[k], c_ref[k], c_live[k]);
                    break;
                }
            printf("\n");
        }
    }
}

static unsigned long long rs = 0xD1B54A32D192ED03ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    setvbuf(stdout, NULL, _IONBF, 0);
    live_fsac = (F_Find)GetProcAddress(h, "RtlFindSetBitsAndClear");
    live_fcas = (F_Find)GetProcAddress(h, "RtlFindClearBitsAndSet");
    if (!live_fsac || !live_fcas) { printf("resolve failed\n"); return 1; }

    printf("== CORRECTNESS: RtlFindSetBitsAndClear / RtlFindClearBitsAndSet ==\n");
    printf("   every case runs on THREE copies; the answers AND the whole buffers are compared\n");

    /* ---- 1. exhaustive over every 16-bit bitmap ---- */
    {
        long before = cases;
        ULONG v, src[2], n, hint;
        for (v = 0; v < 65536; ++v) {
            src[0] = v; src[1] = 0u;
            for (n = 0; n <= 9; n += 3)
                for (hint = 0; hint <= 12; hint += 6) {
                    one(src, 2, 16, n, hint, 0, "exhaustive 16-bit");
                    one(src, 2, 16, n, hint, 1, "exhaustive 16-bit");
                }
        }
        printf("  1. all 65536 16-bit bitmaps x N 0,3,6,9 x hint 0,6,12, both exports: %ld\n",
               cases - before);
    }

    /* ---- 2. one planted run, asked with every N around its length ---- */
    {
        long before = cases;
        static ULONG src[64];
        ULONG at, len, n, hint;
        int i;
        for (at = 28; at <= 40; ++at)
            for (len = 1; len <= 40; len += 3) {
                for (i = 0; i < 64; ++i) src[i] = 0u;
                for (i = 0; i < (int)len; ++i) src[(at + i) >> 5] |= 1u << ((at + i) & 31);
                for (n = (len > 3 ? len - 3 : 0); n <= len + 3; ++n)
                    for (hint = 0; hint <= 96; hint += 32) {
                        one(src, 64, 2048, n, hint, 0, "one run, N around its length");
                        for (i = 0; i < 64; ++i) src[i] = ~src[i];
                        one(src, 64, 2048, n, hint, 1, "one run, N around its length");
                        for (i = 0; i < 64; ++i) src[i] = ~src[i];
                    }
            }
        printf("  2. one run at 13 positions x 14 lengths, asked with N from len-3 to len+3 --\n"
               "     EXACTLY N bits are written, not the whole run, and this is where that lives: %ld\n",
               cases - before);
    }

    /* ---- 3. repeated calls until the bitmap is exhausted ---- */
    {
        long before = cases;
        static ULONG src[32];
        static ULONG ours[32], refb[32], livb[32];
        RBM a, b, c;
        int trial, i, step;
        long drained = 0;
        for (trial = 0; trial < 300; ++trial) {
            ULONG n = 1 + (rnd() % 12);
            for (i = 0; i < 32; ++i) src[i] = (ULONG)(rnd() | (rnd() & rnd()));
            memcpy(ours, src, sizeof src);
            memcpy(refb, src, sizeof src);
            memcpy(livb, src, sizeof src);
            a.SizeOfBitMap = 1024; a.Buffer = ours;
            b.SizeOfBitMap = 1024; b.Buffer = refb;
            c.SizeOfBitMap = 1024; c.Buffer = livb;
            for (step = 0; step < 200; ++step) {
                ULONG hint = rnd() % 1100;
                ULONG ro = wia_findsetbitsandclear(&a, n, hint);
                ULONG rr = ref_findsetbitsandclear(&b, n, hint);
                ULONG rl = live_fsac(&c, n, hint);
                ++cases;
                if (rl == 0xFFFFFFFFul) ++n_none; else ++n_found;
                if (ro != rl || rr != rl ||
                    memcmp(ours, livb, sizeof ours) || memcmp(refb, livb, sizeof refb)) {
                    if (++fails <= 20)
                        printf("  MISMATCH [drain] trial=%d step=%d N=%lu hint=%lu "
                               "ours=%lu ref=%lu live=%lu\n", trial, step, n, hint, ro, rr, rl);
                    break;
                }
                if (rl == 0xFFFFFFFFul) { ++drained; break; }
            }
        }
        printf("  3. repeated calls on ONE bitmap until it is exhausted (%ld of 300 ran dry):\n"
               "     each call must consume exactly what it claims, or the next answer is wrong: %ld\n",
               drained, cases - before);
    }

    /* ---- 4. the wrap ---- */
    {
        long before = cases;
        static ULONG src[64];
        ULONG at, n, hint;
        int i;
        for (at = 0; at < 200; at += 7)
            for (n = 1; n <= 16; n += 3) {
                ULONG len = n + (at % 5);
                for (i = 0; i < 64; ++i) src[i] = 0u;
                for (i = 0; i < (int)len; ++i) src[(at + i) >> 5] |= 1u << ((at + i) & 31);
                for (hint = 300; hint <= 2000; hint += 340) {
                    one(src, 64, 2048, n, hint, 0, "the wrapped answer");
                    for (i = 0; i < 64; ++i) src[i] = ~src[i];
                    one(src, 64, 2048, n, hint, 1, "the wrapped answer");
                    for (i = 0; i < 64; ++i) src[i] = ~src[i];
                }
            }
        /* and a run straddling the wrap point, which must NOT count */
        for (i = 0; i < 64; ++i) src[i] = 0u;
        for (i = 2044; i < 2048; ++i) src[i >> 5] |= 1u << (i & 31);
        for (i = 0; i < 4; ++i) src[i >> 5] |= 1u << (i & 31);
        for (hint = 1900; hint <= 2047; hint += 37)
            one(src, 64, 2048, 8, hint, 0, "a run straddling the wrap point");
        printf("  4. the only run is BEHIND the hint (it must still mutate), and one straddling\n"
               "     the wrap point (which must not count at all): %ld\n", cases - before);
    }

    /* ---- 5. a guard page: this function WRITES ---- */
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
            ULONG nw, sz, k, n, hint;
            static ULONG src[64];
            for (nw = 1; nw <= 33; ++nw) {
                ULONG* live_buf = (ULONG*)(base + si.dwPageSize) - nw;
                for (k = 0; k < nw; ++k) src[k] = (k & 1) ? 0xFFFFFFFFu : 0xF0F0F0F0u;
                for (sz = (nw - 1) * 32 + 1; sz <= nw * 32; sz += 5)
                    for (n = 0; n <= 40; n += 5)
                        for (hint = 0; hint < sz; hint += 17) {
                            /* the live export runs on the guarded buffer, ours and the oracle on
                               their own copies -- all three are compared as usual */
                            RBM a, b, c;
                            ULONG ro, rr, rl;
                            memcpy(c_ours, src, nw * sizeof(ULONG));
                            memcpy(c_ref,  src, nw * sizeof(ULONG));
                            memcpy(live_buf, src, nw * sizeof(ULONG));
                            a.SizeOfBitMap = sz; a.Buffer = c_ours;
                            b.SizeOfBitMap = sz; b.Buffer = c_ref;
                            c.SizeOfBitMap = sz; c.Buffer = live_buf;
                            ro = wia_findsetbitsandclear(&a, n, hint);
                            rr = ref_findsetbitsandclear(&b, n, hint);
                            rl = live_fsac(&c, n, hint);
                            ++cases; ++guard;
                            if (n == 0) ++n_zero; else if (rl == 0xFFFFFFFFul) ++n_none; else ++n_found;
                            if (ro != rl || rr != rl ||
                                memcmp(c_ours, live_buf, nw * sizeof(ULONG)) ||
                                memcmp(c_ref,  live_buf, nw * sizeof(ULONG)))
                                if (++fails <= 20)
                                    printf("  MISMATCH [guard page] nw=%lu size=%lu N=%lu hint=%lu "
                                           "ours=%lu ref=%lu live=%lu\n", nw, sz, n, hint, ro, rr, rl);
                        }
            }
            printf("  5. a PAGE_NOACCESS page at the end of the buffer, 1..33 ULONGs -- and this\n"
                   "     function WRITES, so an overrun is a fault: %ld cases, no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* ---- 6. randomised ---- */
    {
        long before = cases;
        static ULONG src[128];
        int trial, i, d;
        for (trial = 0; trial < 80000; ++trial) {
            ULONG sz, n, hint;
            d = (int)(rnd() % 5);
            for (i = 0; i < 128; ++i)
                src[i] = (d == 0) ? 0u : (d == 1) ? 0xFFFFFFFFu
                       : (d == 2) ? (ULONG)(rnd() | rnd())
                       : (d == 3) ? (ULONG)(rnd() & rnd())
                                  : (ULONG)rnd();
            sz   = 1 + (rnd() % 4096);
            n    = (rnd() & 3) ? 1 + (rnd() % 24) : rnd() % 90;
            hint = rnd() % (sz + 64);
            one(src, 128, sz, n, hint, (int)(rnd() & 1), "randomised");
        }
        printf("  6. randomised, 5 densities, N 0..89, hints past the end: %ld\n", cases - before);
    }

    /* ---- 7. a NULL RTL_BITMAP ----
       NOT compared against the live export: the shipped code dereferences rcx on its first
       instruction, so there is only a fault to reproduce and this does not reproduce it (the same
       position changes 259 and 192 took). What IS checked is that our two paths agree with each
       other and with change 256, which answers NOT FOUND. The fast path added for bitmaps of 64
       bits or fewer reads SizeOfBitMap before anything else, so it would have faulted here while
       the general path returned -1 -- a function that answered -1 for a big NULL bitmap and
       faulted for a small one. No corpus passed NULL, so nothing caught it until this. */
    {
        ULONG a = wia_findsetbitsandclear(NULL, 8, 0);
        ULONG b = wia_findclearbitsandset(NULL, 8, 0);
        ULONG c = wia_findsetbitsandclear(NULL, 0, 40);
        ULONG d = ref_findsetbitsandclear_null_expect();
        ++cases; ++cases; ++cases;
        printf("  7. a NULL bitmap, both exports and N=0: %lu / %lu / %lu (expected %lu each)\n",
               a, b, c, d);
        if (a != d || b != d || c != d) {
            ++fails;
            printf("  MISMATCH [NULL bitmap] the two paths do not agree\n");
        }
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf("  the three arms: FOUND (and therefore wrote) %ld, NOT FOUND %ld, N=0 %ld\n",
           n_found, n_none, n_zero);
    if (!n_found || !n_none || !n_zero) {
        printf("CORRECTNESS: FAILED (an arm was never reached)\n");
        return 1;
    }
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (answer AND whole buffer exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
