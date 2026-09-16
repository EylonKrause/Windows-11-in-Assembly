/* changes/258-rtlfindclearruns/probes/contract.c
 *
 * ntdll!RtlFindClearRuns -- "find up to SizeOfRunArray runs of clear bits, optionally the LONGEST
 * ones". This is the function change 255's target turned out to be a wrapper around:
 * RtlFindLongestRunClear (RVA 0x0E3240) is nine instructions around
 * RtlFindClearRuns(bitmap, buf, 1, TRUE).
 *
 * WHY IT IS WORTH ITS OWN CHANGE. discovery/ntdll_bitmap.c measured the same call EIGHTY TIMES APART
 * depending on one BOOLEAN:
 *
 *       RtlFindClearRuns 64, UNSORTED, sparse       144.83 ns   -- stops when the array fills
 *       RtlFindClearRuns  1, SORTED,   sparse     13457.57 ns   -- must examine everything
 *       RtlFindClearRuns 64, SORTED,   sparse     14399.10 ns
 *
 * The unsorted form returns as soon as it has enough runs, which on a bitmap with a run every two
 * bits is after about 0.2% of it. The sorted form cannot: to know the LONGEST runs it has to see
 * them all. So the sorted path is a full scan at roughly one bit per cycle, exactly like change
 * 255's, and change 255's machinery -- the per-word scan with the bounded `x &= x >> 1` search and
 * the two vector skips -- applies directly.
 *
 * WHAT HAS TO BE PINNED, because "up to N runs, optionally sorted" leaves a great deal unsaid:
 *
 *   1. THE SHAPE OF THE ARRAY. RtlFindLongestRunClear reads [rsp+0x40] as the start and [rsp+0x44]
 *      as the length, so the entry is two ULONGs -- but which order, and is it really two?
 *   2. SORTED BY WHAT, AND WHICH WAY? Length descending is the obvious reading. What breaks a tie
 *      between two runs of equal length -- the earlier one, or the later?
 *   3. WHEN THE ARRAY IS SMALLER THAN THE NUMBER OF RUNS, does the UNSORTED form return the FIRST
 *      runs or an arbitrary subset? And does the sorted form return the longest N in order?
 *   4. THE DEGENERATE CASES: SizeOfRunArray = 0, a bitmap with no clear bits at all, and a bitmap
 *      that is entirely clear.
 *
 * ------------------------------------------------------------------------------------------------
 * A CORRECTION, 2026-09-16. This probe concluded from section 3 that the UNSORTED form "returns the
 * FIRST runs found, in order". The first half is right and THE SECOND HALF IS WRONG: the order runs
 * are found in is not left to right. Every run in section 3 sits in a byte of its own, which is the
 * one arrangement where the two orders agree, so the rows below are all true and the conclusion
 * drawn from them was not. The correctness corpus caught it; probes/enumorder.c pins the real rule
 * -- a byte at a time, the carried run first, then the byte's INTERIOR runs LONGEST FIRST -- against
 * the live export over 1,567,328 cases. The rows printed here are left exactly as they were.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef struct { ULONG StartingIndex; ULONG NumberOfBits; } RUN;
typedef ULONG (NTAPI *F_Runs)(RBM*, RUN*, ULONG, BOOLEAN);

static F_Runs fcr;
static ULONG buf[32];
static RUN   out[64];
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("  FAIL: "); printf(__VA_ARGS__); \
                                       printf("\n"); } } while (0)

static void allset(void) { int i; for (i = 0; i < 32; ++i) buf[i] = 0xFFFFFFFFu; }
static void crun(int s, int n) { int i; for (i = s; i < s + n; ++i) buf[i >> 5] &= ~(1u << (i & 31)); }

static void show(const char* what, RBM* bm, ULONG cap, BOOLEAN sorted)
{
    ULONG n, i;
    memset(out, 0xEE, sizeof out);
    n = fcr(bm, out, cap, sorted);
    printf("   %-34s cap=%-3lu sorted=%d -> %lu run(s):", what, cap, (int)sorted, n);
    for (i = 0; i < n && i < 10; ++i)
        printf(" (%lu,%lu)", out[i].StartingIndex, out[i].NumberOfBits);
    printf("\n");
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    RBM bm;
    setvbuf(stdout, NULL, _IONBF, 0);   /* a probe that crashes must still say how far it got */
    fcr = (F_Runs)GetProcAddress(h, "RtlFindClearRuns");
    if (!fcr) { printf("RtlFindClearRuns not found\n"); return 1; }
    bm.Buffer = buf; bm.SizeOfBitMap = 1024;

    printf("RtlFindClearRuns -- the contract\n");
    printf("(entries printed as (StartingIndex, NumberOfBits))\n\n");

    /* ---- 1. the array shape, and 2. the sort order ---- */
    {
        printf("1+2. THE ARRAY SHAPE AND THE SORT ORDER\n");
        allset();
        crun(100, 3);      /* three runs of distinct lengths, deliberately out of order */
        crun(200, 9);
        crun(300, 5);
        show("runs of 3, 9, 5 at 100/200/300", &bm, 8, FALSE);
        show("... the same, sorted", &bm, 8, TRUE);
        printf("   => the pair is (start, length) if the 9-run reads (200,9)\n");
        printf("   => sorted is by LENGTH DESCENDING if the sorted list starts with (200,9)\n\n");
    }

    /* ---- 2b. the tie-break ---- */
    {
        printf("2b. THE TIE-BREAK between equal-length runs\n");
        allset();
        crun(100, 4);
        crun(300, 4);
        crun(500, 4);
        show("three runs of 4, at 100/300/500", &bm, 8, TRUE);
        printf("   => the FIRST wins a tie if the list reads (100,4) (300,4) (500,4)\n\n");
    }

    /* ---- 3. an array smaller than the number of runs ---- */
    {
        printf("3. AN ARRAY SMALLER THAN THE NUMBER OF RUNS\n");
        allset();
        crun(100, 3);
        crun(200, 9);
        crun(300, 5);
        crun(400, 7);
        crun(500, 1);
        show("5 runs, cap 2, UNSORTED", &bm, 2, FALSE);
        show("5 runs, cap 2, SORTED",   &bm, 2, TRUE);
        show("5 runs, cap 3, SORTED",   &bm, 3, TRUE);
        show("5 runs, cap 5, SORTED",   &bm, 5, TRUE);
        show("5 runs, cap 9, SORTED",   &bm, 9, TRUE);
        printf("   => UNSORTED returns the first ones FOUND; SORTED returns the LONGEST\n");
        printf("      (and the order they are FOUND in is not left to right: see enumorder.c)\n\n");
    }

    /* ---- 4. the degenerate cases ---- */
    {
        printf("4. THE DEGENERATE CASES\n");
        allset();
        show("no clear bits at all, cap 4", &bm, 4, TRUE);
        show("no clear bits at all, cap 0", &bm, 0, TRUE);
        crun(0, 1024);
        show("entirely clear, cap 4", &bm, 4, TRUE);
        allset();
        crun(500, 6);
        /* SizeOfRunArray = 0 WITH A RUN PRESENT CRASHES THE SHIPPED EXPORT. With no clear bits at
           all it returns 0 quite happily (the row above), so the zero capacity is not rejected --
           it is simply not survived once there is something to report. Verified by this probe
           faulting here with an access violation; the case is therefore excluded from every corpus,
           the way NULL is excluded from change 253's, and our implementation is free to differ. */
        printf("   one run, cap 0, SORTED             -- SKIPPED: the SHIPPED export FAULTS here\n");
        printf("   one run, cap 0, UNSORTED           -- SKIPPED: same\n");
        show("one run, cap 1", &bm, 1, TRUE);
        bm.SizeOfBitMap = 0;
        show("SizeOfBitMap = 0", &bm, 4, TRUE);
        bm.SizeOfBitMap = 1024;
        printf("\n");
    }

    /* ---- 5. a run that ends exactly at the last bit, and the slack ---- */
    {
        printf("5. THE LAST BIT AND THE SLACK PAST SizeOfBitMap\n");
        allset();
        crun(1018, 6);                    /* ends exactly at bit 1023 */
        show("clear 1018..1023, size 1024", &bm, 4, TRUE);
        bm.SizeOfBitMap = 1020;
        show("... the same buffer declared as 1020", &bm, 4, TRUE);
        printf("   => the slack is masked if the second reads (1018,2)\n");
        bm.SizeOfBitMap = 1024;
        allset();
        for (;;) { crun(0, 1024); break; }
        bm.SizeOfBitMap = 33;
        show("entirely clear, declared 33", &bm, 4, TRUE);
        bm.SizeOfBitMap = 1024;
        printf("\n");
    }

    printf(fails ? "CONTRACT: %d CHECK(S) FAILED\n" : "CONTRACT: (read the rows above)\n", fails);
    return 0;
}
