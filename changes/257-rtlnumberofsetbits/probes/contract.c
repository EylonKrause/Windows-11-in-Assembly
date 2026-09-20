/* changes/257-rtlnumberofsetbits/probes/contract.c
 *
 * ntdll!RtlNumberOfSetBits / RtlNumberOfClearBits and their ...InRange forms.
 *
 * WHY. discovery/ntdll_bitmap.c measured the whole-bitmap forms at 413 ns over a 64 Kbit map --
 * 0.050 ns/byte, about two cycles per 64-BIT word. The shipped code already uses the right
 * instruction (`popcnt rax, rax` at RVA 0x0F2F33, with byte-table lookups for the ragged ends), so
 * this is not a case of a missing intrinsic; two cycles per word is what a SERIAL accumulator chain
 * costs, because POPCNT has about three cycles of latency and one per cycle of throughput. Breaking
 * that chain, or leaving the general-purpose registers entirely for a VPSHUFB nibble count, is
 * where the room is.
 *
 * What has to be pinned first, because counting is only simple once the edges are settled:
 *
 *   1. Is the range (start, length) or (start, end)? The survey called it with (100, 60000) on a
 *      half-set bitmap and got 30000, which is 60000/2 and not 59900/2 -- so LENGTH. That was one
 *      observation; this sweeps it.
 *   2. What happens past SizeOfBitMap -- a range that runs off the end, a start at or past it, and
 *      a length of zero. The slack in the final ULONG is set to 1 here deliberately, so any failure
 *      to mask shows up as a larger count.
 *   3. DOES ClearBits = SizeOfBitMap - SetBits exactly, including for ranges? If so one core
 *      serves all four exports.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Count)(RBM*);
typedef ULONG (NTAPI *F_Range)(RBM*, ULONG, ULONG);

static ULONG buf[32];
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("  FAIL: "); printf(__VA_ARGS__); \
                                       printf("\n"); } } while (0)

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Count nset = (F_Count)GetProcAddress(h, "RtlNumberOfSetBits");
    F_Count nclr = (F_Count)GetProcAddress(h, "RtlNumberOfClearBits");
    F_Range rset = (F_Range)GetProcAddress(h, "RtlNumberOfSetBitsInRange");
    F_Range rclr = (F_Range)GetProcAddress(h, "RtlNumberOfClearBitsInRange");
    RBM bm;
    int i;

    if (!nset || !nclr) { printf("resolve failed\n"); return 1; }
    printf("RtlNumberOfSetBits family -- the contract\n");
    printf("InRange present: set=%s clear=%s\n\n", rset ? "yes" : "NO", rclr ? "yes" : "NO");

    bm.Buffer = buf;

    /* ---- 1. (start, LENGTH) or (start, END)? ---- */
    if (rset) {
        printf("1. IS THE SECOND PAIR (start, LENGTH) OR (start, END)?\n");
        for (i = 0; i < 32; ++i) buf[i] = 0xFFFFFFFFu;   /* every bit set: the count IS the span */
        bm.SizeOfBitMap = 1024;
        printf("   all bits set, size 1024:\n");
        printf("     (0, 100)   -> %lu   (length => 100, end => 100)\n", rset(&bm, 0, 100));
        printf("     (100, 100) -> %lu   (length => 100, end => 0)\n",   rset(&bm, 100, 100));
        printf("     (100, 300) -> %lu   (length => 300, end => 200)\n", rset(&bm, 100, 300));
        CHECK(rset(&bm, 100, 300) == 300, "not (start, LENGTH)");
        printf("\n");
    }

    /* ---- 2. the edges ---- */
    {
        printf("2. THE EDGES -- the slack in the last ULONG is SET, so a missing mask shows up\n");
        for (i = 0; i < 32; ++i) buf[i] = 0xFFFFFFFFu;
        for (i = 1; i <= 40; ++i) {
            bm.SizeOfBitMap = (ULONG)i;
            if (nset(&bm) != (ULONG)i) {
                printf("   size=%d -> %lu   <== the slack past SizeOfBitMap IS being counted\n",
                       i, nset(&bm));
                ++fails;
            }
        }
        printf("   sizes 1..40 over an all-ones buffer: every one counts exactly its own size\n");
        bm.SizeOfBitMap = 0;
        printf("   size 0            -> set=%lu clear=%lu\n", nset(&bm), nclr(&bm));
        bm.SizeOfBitMap = 100;
        if (rset) {
            printf("   (0, 0)            -> %lu\n", rset(&bm, 0, 0));
            printf("   (50, 500) past the end -> %lu  (clamped to %d)\n", rset(&bm, 50, 500), 50);
            printf("   (100, 10) at the end   -> %lu\n", rset(&bm, 100, 10));
            printf("   (200, 10) past the end -> %lu\n", rset(&bm, 200, 10));
        }
        printf("\n");
    }

    /* ---- 2b. exactly WHEN does the range form refuse? ---- */
    if (rset) {
        ULONG st, ln;
        int bad = 0;
        printf("2b. THE REFUSAL PREDICATE. Not a clamp -- it returns 0xFFFFFFFF. Model:\n");
        printf("    refuse iff (length == 0) or (start + length > SizeOfBitMap)\n");
        for (i = 0; i < 32; ++i) buf[i] = 0xFFFFFFFFu;
        bm.SizeOfBitMap = 100;
        printf("    (0,1)=%ld (99,1)=%ld (99,2)=%ld (100,0)=%ld (0,100)=%ld (0,101)=%ld\n",
               (long)rset(&bm, 0, 1), (long)rset(&bm, 99, 1), (long)rset(&bm, 99, 2),
               (long)rset(&bm, 100, 0), (long)rset(&bm, 0, 100), (long)rset(&bm, 0, 101));
        for (st = 0; st <= 110; ++st)
            for (ln = 0; ln <= 110; ++ln) {
                ULONG got = rset(&bm, st, ln);
                ULONG want = (ln == 0 || st + ln > bm.SizeOfBitMap) ? 0xFFFFFFFFu : ln;
                if (got != want) {
                    if (bad < 6) printf("    MODEL MISS: (%lu,%lu) -> %lu, model says %lu\n",
                                        st, ln, got, want);
                    ++bad;
                }
            }
        printf("    111x111 combinations: %d disagree with the model\n\n", bad);
        CHECK(bad == 0, "the refusal predicate is not (len==0 || start+len > size)");
    }

    /* ---- 3. does clear == size - set, always? ---- */
    {
        unsigned long long rs = 0x9E3779B97F4A7C15ull;
        int bad = 0, badr = 0, t;
        printf("3. IS clear == size - set, EXACTLY? (if so, one core serves all four exports)\n");
        for (t = 0; t < 20000; ++t) {
            ULONG sz, st, ln;
            rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
            for (i = 0; i < 32; ++i) {
                rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
                buf[i] = (ULONG)(rs >> 32);
            }
            sz = 1 + (ULONG)((rs >> 11) % 1024);
            bm.SizeOfBitMap = sz;
            if (nclr(&bm) != sz - nset(&bm)) ++bad;
            if (rset && rclr) {
                st = (ULONG)((rs >> 17) % (sz + 20));
                ln = (ULONG)((rs >> 23) % (sz + 20));
                {
                    ULONG a = rset(&bm, st, ln), b = rclr(&bm, st, ln);
                    ULONG span = (st >= sz) ? 0 : ((st + ln > sz) ? sz - st : ln);
                    if (a + b != span) ++badr;
                }
            }
        }
        printf("   20000 random bitmaps: %d where clear != size - set\n", bad);
        printf("   20000 random ranges : %d where set+clear != the clamped span\n", badr);
        CHECK(bad == 0, "clear is not size - set");
        printf("\n");
    }

    printf(fails ? "CONTRACT: %d CHECK(S) FAILED\n" : "CONTRACT: PASS\n", fails);
    return fails ? 1 : 0;
}
