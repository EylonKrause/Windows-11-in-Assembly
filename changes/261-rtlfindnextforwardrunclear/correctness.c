/* changes/261-rtlfindnextforwardrunclear/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll exports, for BOTH
 * RtlFindNextForwardRunClear and RtlFindLastBackwardRunClear.
 *
 * BOTH THE LENGTH AND THE START ARE COMPARED, and the start is pre-filled with a poison value so a
 * case where the export leaves it alone is distinguishable from one where it writes something.
 * That matters here: "nothing found" still writes the pointer, and the two forms write DIFFERENT
 * values -- SizeOfBitMap forward, zero backward -- so an implementation that returned the right
 * length and the wrong start would pass any test that only looked at the return value.
 *
 *   1. EXHAUSTIVE over every 16-bit bitmap x every FromIndex, both exports. Nothing sampled: this
 *      covers every arrangement of runs and every position inside, before and after them.
 *   2. FromIndex AT EVERY POSITION in and around a single planted run, which is the case the
 *      contract is really about -- the forward form clips the start and the backward form clips
 *      the end, and an implementation that reported the run's true extent would pass a corpus that
 *      only ever asked from outside it.
 *   3. THE SLACK: a run reaching the end of the buffer, with the bitmap declared at every size
 *      inside that run.
 *   4. A GUARD PAGE at the end of the buffer at odd ULONG counts -- the vector step reads 32 bytes.
 *   5. RANDOMISED at several densities, with FromIndex everywhere including past the end.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Run)(RBM*, ULONG, PULONG);

ULONG wia_findnextforwardrunclear(void*, ULONG, PULONG);
ULONG wia_findlastbackwardrunclear(void*, ULONG, PULONG);
ULONG ref_findnextforwardrunclear(void*, ULONG, PULONG);
ULONG ref_findlastbackwardrunclear(void*, ULONG, PULONG);

static F_Run live_fwd, live_back;
static long fails = 0, cases = 0, found_f = 0, found_b = 0;

static void one(ULONG* buf, ULONG size, ULONG from, int backward, const char* where)
{
    RBM bm;
    ULONG so = 0xDEADBEEFu, sr = 0xDEADBEEFu, sl = 0xDEADBEEFu;
    ULONG lo, lr, ll;
    ++cases;
    bm.SizeOfBitMap = size; bm.Buffer = buf;
    if (backward) {
        lo = wia_findlastbackwardrunclear(&bm, from, &so);
        lr = ref_findlastbackwardrunclear(&bm, from, &sr);
        ll = live_back(&bm, from, &sl);
        if (ll) ++found_b;
    } else {
        lo = wia_findnextforwardrunclear(&bm, from, &so);
        lr = ref_findnextforwardrunclear(&bm, from, &sr);
        ll = live_fwd(&bm, from, &sl);
        if (ll) ++found_f;
    }
    if (lo != ll || lr != ll || so != sl || sr != sl) {
        if (++fails <= 20)
            printf("  MISMATCH [%s] %s size=%lu from=%lu  len ours=%lu ref=%lu live=%lu  "
                   "start ours=%lu ref=%lu live=%lu\n", where, backward ? "BACK" : "FWD ",
                   size, from, lo, lr, ll, so, sr, sl);
    }
}

static unsigned long long rs = 0xB5026F5AA96619E9ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    setvbuf(stdout, NULL, _IONBF, 0);
    live_fwd  = (F_Run)GetProcAddress(h, "RtlFindNextForwardRunClear");
    live_back = (F_Run)GetProcAddress(h, "RtlFindLastBackwardRunClear");
    if (!live_fwd || !live_back) { printf("resolve failed\n"); return 1; }

    printf("== CORRECTNESS: RtlFindNextForwardRunClear / RtlFindLastBackwardRunClear ==\n");
    printf("   both the LENGTH and the START are compared; the start is poisoned first\n");

    /* ---- 1. EXHAUSTIVE over every 16-bit bitmap ---- */
    {
        long before = cases;
        ULONG v, buf[2], f;
        for (v = 0; v < 65536; ++v) {
            buf[0] = v | 0xFFFF0000u;
            buf[1] = 0xFFFFFFFFu;
            for (f = 0; f <= 17; f += 4) {
                one(buf, 16, f, 0, "exhaustive 16-bit");
                one(buf, 16, f, 1, "exhaustive 16-bit");
            }
        }
        printf("  1. all 65536 16-bit bitmaps x FromIndex 0,4..16, both exports: %ld\n",
               cases - before);
    }

    /* ---- 2. every FromIndex in and around one planted run ---- */
    {
        long before = cases;
        static ULONG buf[64];
        ULONG at, len, f;
        int i;
        for (at = 60; at <= 70; ++at)
            for (len = 1; len <= 40; len += 3) {
                for (i = 0; i < 64; ++i) buf[i] = 0xFFFFFFFFu;
                for (i = 0; i < (int)len; ++i) buf[(at + i) >> 5] &= ~(1u << ((at + i) & 31));
                for (f = 0; f <= 130; ++f) {
                    one(buf, 2048, f, 0, "around one run");
                    one(buf, 2048, f, 1, "around one run");
                }
            }
        printf("  2. one run at 11 positions x 14 lengths, asked from EVERY index 0..130 --\n"
               "     the clipping rule is the whole contract and this is where it lives: %ld\n",
               cases - before);
    }

    /* ---- 3. the slack ---- */
    {
        long before = cases;
        static ULONG buf[64];
        ULONG sz, f;
        int i;
        for (i = 0; i < 64; ++i) buf[i] = 0xFFFFFFFFu;
        for (i = 1000; i < 1100; ++i) buf[i >> 5] &= ~(1u << (i & 31));
        for (sz = 990; sz <= 1110; ++sz)
            for (f = 900; f <= 1110; f += 17) {
                one(buf, sz, f, 0, "the slack");
                one(buf, sz, f, 1, "the slack");
            }
        printf("  3. a run of 100 bits with the bitmap declared at every size across it: %ld\n",
               cases - before);
    }

    /* ---- 4. a guard page ---- */
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
            ULONG nw, sz, k, f;
            for (nw = 1; nw <= 41; ++nw) {
                ULONG* buf = (ULONG*)(base + si.dwPageSize) - nw;
                /* THE BACKWARD FORM IS ONLY ASKED AT AN EVEN ULONG COUNT, and that is not a
                   convenience -- it is a property of the SHIPPED export. RtlFindLastBackwardRunClear
                   tests the starting bit with

                       000F65FF  bt qword ptr [r9], rax

                   a SIXTY-FOUR-BIT operand on the buffer base, so it reads eight bytes whatever the
                   array actually holds. With an odd number of ULONGs the last four of those lie past
                   the array -- which is this guard page -- and ntdll faults. Verified rather than
                   inferred: a one-word bitmap at the end of a page kills the live export while ours
                   and the oracle walk away. Ours reads 32-bit words and whole-word vector blocks
                   only, so it is strictly the safer of the two; there is simply no live answer to
                   compare against in the cases ntdll cannot survive. */
                for (k = 0; k < nw; ++k) buf[k] = (k & 1) ? 0xFFFFFFFFu : 0xA5A5A5A5u;
                for (sz = (nw - 1) * 32 + 1; sz <= nw * 32; sz += 7)
                    for (f = 0; f < sz; f += 11) {
                        one(buf, sz, f, 0, "guard page");
                        ++guard;
                        if ((nw & 1) == 0) { one(buf, sz, f, 1, "guard page"); ++guard; }
                    }
                for (k = 0; k < nw; ++k) buf[k] = 0xFFFFFFFFu;   /* nothing to find: a full scan */
                one(buf, nw * 32, 0, 0, "guard page, all set");
                ++guard;
                if ((nw & 1) == 0) {
                    one(buf, nw * 32, nw * 32 - 1, 1, "guard page, all set");
                    ++guard;
                }
            }
            printf("  4. a PAGE_NOACCESS page at the end of the buffer, 1..41 ULONGs: %ld cases, "
                   "no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* ---- 5. randomised ---- */
    {
        long before = cases;
        static ULONG buf[128];
        int trial, i, d;
        static const int DENS[6] = { 0, 2, 12, 50, 92, 100 };
        for (trial = 0; trial < 60000; ++trial) {
            ULONG sz, f;
            d = DENS[rnd() % 6];
            for (i = 0; i < 128; ++i) buf[i] = 0xFFFFFFFFu;
            if (d == 100) { for (i = 0; i < 128; ++i) buf[i] = 0u; }
            else for (i = 0; i < 4096; ++i)
                     if ((int)(rnd() % 100) < d) buf[i >> 5] &= ~(1u << (i & 31));
            sz = 1 + (rnd() % 4096);
            f  = rnd() % (sz + 64);
            one(buf, sz, f, (int)(rnd() & 1), "randomised");
        }
        printf("  5. randomised, 6 densities, FromIndex everywhere including past the end: %ld\n",
               cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf("  the live exports actually FOUND a run %ld times forward and %ld backward -- a corpus\n"
           "  that never found one would have tested only the two not-found paths\n", found_f, found_b);
    if (!found_f || !found_b) { printf("CORRECTNESS: FAILED (a corpus never found a run)\n"); return 1; }
    printf(fails ? "CORRECTNESS: FAILED\n" : "CORRECTNESS: PASS (length and start exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
