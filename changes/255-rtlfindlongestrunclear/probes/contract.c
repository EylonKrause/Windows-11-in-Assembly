/* changes/255-rtlfindlongestrunclear/probes/contract.c
 *
 * ntdll!RtlFindLongestRunClear -- the contract, and the three things that decide the implementation.
 *
 * Why this target. discovery/ntdll_bitmap.c measured it at ~1.03 ns/byte on a 64 Kbit bitmap, and
 * the crucial observation is that the cost is not per-run: eight clear runs cost 8423 ns and two
 * hundred cost 8689, so the ~1 ns/byte is the scan itself -- roughly one bit per cycle over 65536
 * bits. That is twice the per-byte cost of change 252's target and the most expensive thing left in
 * ntdll. RVA 0x0E3240 is nine instructions around RtlFindClearRuns(bitmap, buf, 1, TRUE), so the
 * cost is entirely in FindClearRuns with SortByLength set.
 *
 * The three questions that shape the code:
 *
 *   1. Which run wins a tie? The sparse survey bitmap (0xA5A5A5A5) has sixteen thousand clear runs
 *      of length two and it reported "len=2 at 3" -- the FIRST of them, since 0xA5 is 1010 0101 and
 *      LSB-first its clear runs are bit 1 (length 1), bits 3-4 (length 2), bit 6 (length 1). If the
 *      LAST tie won instead, an implementation that updates its best on ">" would be wrong
 *      everywhere and would still pass any test whose bitmap had a unique longest run.
 *
 *   2. What happens with no clear bits at all, and what is written to *StartingIndex then? The
 *      wrapper does `and dword ptr [rbx], 0` on the failure path, which reads as "writes 0" -- but
 *      that is one reading of one instruction and it is worth ten lines to be sure.
 *
 *   3. Are the bits past SizeOfBitMap counted? a bitmap whose size is not a multiple of 32 has
 *      slack in its last word. If the shipped code includes that slack, a clear run can run off the
 *      end of the declared size; if it masks it, it cannot. An implementation reading 64 bits at a
 *      time has to know which, and the answer is not guessable.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Longest)(RBM*, PULONG);
typedef ULONG (NTAPI *F_Runs)(RBM*, PULONG, ULONG, BOOLEAN);

static F_Longest flr;
static F_Runs    fcr;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("  FAIL: "); printf(__VA_ARGS__); \
                                       printf("\n"); } } while (0)

#define W 64
static ULONG buf[W];
static RBM   bm;

static void setall(ULONG v) { int i; for (i = 0; i < W; ++i) buf[i] = v; }
static void clearbit(int b)  { buf[b >> 5] &= ~(1u << (b & 31)); }
static void setbit(int b)    { buf[b >> 5] |=  (1u << (b & 31)); }

static ULONG run(ULONG size, ULONG* ix)
{
    bm.SizeOfBitMap = size; bm.Buffer = buf;
    *ix = 0xCCCCCCCCu;
    return flr(&bm, ix);
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    ULONG ix;
    int i;
    flr = (F_Longest)GetProcAddress(h, "RtlFindLongestRunClear");
    fcr = (F_Runs)   GetProcAddress(h, "RtlFindClearRuns");
    if (!flr) { printf("RtlFindLongestRunClear not found\n"); return 1; }

    printf("RtlFindLongestRunClear -- the contract\n\n");

    /* ---- 1. the tie-break ---- */
    {
        ULONG len;
        printf("1. WHICH RUN WINS A TIE?\n");
        setall(0xFFFFFFFFu);
        clearbit(10); clearbit(11);          /* a run of 2 at bit 10 */
        clearbit(50); clearbit(51);          /* another run of 2 at bit 50 */
        len = run(W * 32, &ix);
        printf("   two runs of length 2, at bits 10 and 50 -> len=%lu at %lu\n", len, ix);
        CHECK(len == 2, "expected length 2, got %lu", len);
        CHECK(ix == 10, "the tie went to %lu, not the FIRST run at 10 -- an implementation that "
                        "updates on '>' would be wrong", ix);

        /* three of them, to be sure it is "first" and not "smallest index by accident" */
        setall(0xFFFFFFFFu);
        clearbit(300); clearbit(301);
        clearbit(20);  clearbit(21);
        clearbit(700); clearbit(701);
        len = run(W * 32, &ix);
        printf("   three runs of length 2, at 20, 300, 700 -> len=%lu at %lu\n\n", len, ix);
        CHECK(ix == 20, "expected 20, got %lu", ix);
    }

    /* ---- 2. no clear bits at all ---- */
    {
        ULONG len;
        printf("2. NO CLEAR BITS AT ALL\n");
        setall(0xFFFFFFFFu);
        len = run(W * 32, &ix);
        printf("   all bits set          -> len=%lu, *StartingIndex=%lu%s\n", len, ix,
               ix == 0xCCCCCCCCu ? "  (UNTOUCHED)" : "");
        CHECK(len == 0, "expected 0, got %lu", len);

        printf("   SizeOfBitMap = 0      -> ");
        len = run(0, &ix);
        printf("len=%lu, *StartingIndex=%lu%s\n", len, ix,
               ix == 0xCCCCCCCCu ? "  (UNTOUCHED)" : "");

        setall(0);
        len = run(W * 32, &ix);
        printf("   all bits CLEAR        -> len=%lu at %lu (expect %d at 0)\n\n", len, ix, W * 32);
        CHECK(len == W * 32, "expected %d, got %lu", W * 32, len);
        CHECK(ix == 0, "expected 0, got %lu", ix);
    }

    /* ---- 3. the slack in the last word ---- */
    {
        ULONG len;
        printf("3. ARE THE BITS PAST SizeOfBitMap COUNTED?\n");
        setall(0xFFFFFFFFu);
        /* declare 40 bits, and clear bits 36..63 -- 4 of which are inside the declared size */
        for (i = 36; i < 64; ++i) clearbit(i);
        len = run(40, &ix);
        printf("   size=40, bits 36..63 clear -> len=%lu at %lu\n", len, ix);
        printf("   %s\n", len == 4 ? "   => the slack is MASKED: only bits 36..39 counted"
                                   : "   => the slack is NOT masked, or the size is rounded");
        CHECK(len == 4, "expected 4 (bits 36..39), got %lu -- the slack handling is not what an "
                        "implementation reading 64 bits at a time can assume", len);

        setall(0xFFFFFFFFu);
        for (i = 0; i < 64; ++i) clearbit(i);
        len = run(33, &ix);
        printf("   size=33, bits 0..63 clear  -> len=%lu at %lu (expect 33 at 0)\n", len, ix);
        CHECK(len == 33, "expected 33, got %lu", len);

        setall(0xFFFFFFFFu);
        clearbit(31); clearbit(32);           /* a run straddling the 32-bit word boundary */
        len = run(W * 32, &ix);
        printf("   a run straddling the word boundary at 31/32 -> len=%lu at %lu\n\n", len, ix);
        CHECK(len == 2 && ix == 31, "expected 2 at 31, got %lu at %lu", len, ix);
    }

    /* ---- 4. runs that straddle a 64-bit boundary, which is what our word size will be ---- */
    {
        ULONG len;
        printf("4. RUNS STRADDLING A 64-BIT BOUNDARY (our word size, not the shipped one's)\n");
        for (i = 60; i <= 70; ++i) {
            int j;
            setall(0xFFFFFFFFu);
            for (j = 60; j < i + 6; ++j) clearbit(j);
            len = run(W * 32, &ix);
            printf("   clear 60..%-3d -> len=%2lu at %lu\n", i + 5, len, ix);
            CHECK(len == (ULONG)(i + 6 - 60) && ix == 60, "expected %d at 60", i + 6 - 60);
        }
        printf("\n");
    }

    /* ---- 5. a long run, and one that ends exactly at the last bit ---- */
    {
        ULONG len;
        printf("5. LONG RUNS AND THE LAST BIT\n");
        setall(0xFFFFFFFFu);
        for (i = 100; i < 900; ++i) clearbit(i);
        len = run(W * 32, &ix);
        printf("   clear 100..899        -> len=%lu at %lu\n", len, ix);
        CHECK(len == 800 && ix == 100, "expected 800 at 100, got %lu at %lu", len, ix);

        setall(0xFFFFFFFFu);
        for (i = W * 32 - 50; i < W * 32; ++i) clearbit(i);
        len = run(W * 32, &ix);
        printf("   the last 50 bits clear -> len=%lu at %lu\n", len, ix);
        CHECK(len == 50, "expected 50, got %lu", len);

        /* a longer run OUTSIDE the declared size must not win */
        setall(0xFFFFFFFFu);
        for (i = 10; i < 20; ++i)   clearbit(i);     /* 10, inside */
        for (i = 100; i < 200; ++i) clearbit(i);     /* 100, OUTSIDE if size = 64 */
        len = run(64, &ix);
        printf("   size=64 with a 100-bit run at 100 -> len=%lu at %lu (expect 10 at 10)\n\n",
               len, ix);
        CHECK(len == 10 && ix == 10, "the run past SizeOfBitMap was counted");
    }

    printf(fails ? "CONTRACT: %d CHECK(S) FAILED\n" : "CONTRACT: PASS\n", fails);
    return fails ? 1 : 0;
}
