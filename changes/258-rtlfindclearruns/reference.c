/* changes/258-rtlfindclearruns/reference.c
 *
 * The independent oracle for RtlFindClearRuns.
 *
 * It shares nothing with impl.asm but the contract. impl.asm scans sixty-four bits at a time for
 * the sorted form and drives the unsorted form off a packed table that answers a whole byte in one
 * load; this walks the bitmap one bit or one byte at a time with nothing packed, nothing skipped
 * and no bit tricks at all.
 *
 * The selection rule, as probes/contract.c and probes/enumorder.c measured it:
 *
 *   * Sorted returns the longest runs, by length descending, and a tie goes to the earlier run.
 *     A stable sort by descending length expresses both halves at once, which is why this uses one
 *     rather than a comparison mentioning the start index: the tie-break is not a secondary key, it
 *     is the order the runs were found in, and stability is exactly that.
 *
 *   * Unsorted returns the first runs found and stops when the array is full, but the order they
 *     Are found in is not left to right. ntdll scans a byte at a time and, within a byte, emits
 *     first the run carried in from earlier bytes, then the runs strictly inside the byte LONGEST
 *     FIRST (ties to the lowest position); the run at the top of the byte is not emitted there at
 *     all, it becomes the carry. So a byte holding a one-bit run at 1 and a two-bit run at 3
 *     reports (3,2) BEFORE (1,1). probes/enumorder.c pins that against the live export over
 *     1,567,328 cases.
 *
 * The two rules meet in one place, and it is worth being explicit about it: for runs of EQUAL
 * length the found order and the ascending order agree (two runs of length L at s1 < s2 end at
 * s1+L < s2+L, so they complete in that order of bytes; inside one byte equal-length interior runs
 * are ordered by position, and a carry run starts at or below the byte's first bit while an
 * interior run starts above it). So the sorted form can be built from the ascending order, and only
 * the unsorted form needs the byte walk.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } REF_RBM;
typedef struct { ULONG StartingIndex; ULONG NumberOfBits; } REF_RUN;

#define REF_MAX 70000

/* ---- the byte helpers, written the slow obvious way ---- */
static int ref_tz8(unsigned b) { int n = 0; while (n < 8 && !((b >> n) & 1)) ++n; return n; }
static int ref_lz8(unsigned b) { int n = 0; while (n < 8 && !((b >> (7 - n)) & 1)) ++n; return n; }
static int ref_longest8(unsigned b)
{
    int best = 0, cur = 0, k;
    for (k = 0; k < 8; ++k) { if ((b >> k) & 1) cur = 0; else { ++cur; if (cur > best) best = cur; } }
    return best;
}

ULONG ref_findclearruns(void* bmv, REF_RUN* out, ULONG cap, BOOLEAN sorted)
{
    static REF_RUN all[REF_MAX];
    REF_RBM* bm = (REF_RBM*)bmv;
    ULONG n = 0, i, written;

    if (!bm || !bm->Buffer || bm->SizeOfBitMap == 0 || cap == 0) return 0;

    if (!sorted) {
        /* The found order, byte by byte, exactly as the shipped scan produces it */
        ULONG k, nbytes = (bm->SizeOfBitMap + 7) >> 3, carry = 0, cstart = 0;
        for (k = 0; k < nbytes; ++k) {
            unsigned b = ((const unsigned char*)bm->Buffer)[k], m;
            int tz, lz;
            if (k == nbytes - 1 && (bm->SizeOfBitMap & 7))
                b |= (0xFFu << (bm->SizeOfBitMap & 7)) & 0xFFu;   /* the slack reads as ONES */
            if (b == 0) { carry += 8; continue; }
            tz = ref_tz8(b);
            if (carry + (ULONG)tz) {                        /* 1. the carried run is complete */
                if (n < REF_MAX) { all[n].StartingIndex = cstart;
                                   all[n].NumberOfBits = carry + tz; ++n; }
            }
            lz = ref_lz8(b);
            cstart = k * 8 + 8 - lz;                        /* 2. the top run becomes the carry */
            carry  = (ULONG)lz;
            m = b | ((1u << tz) - 1u) | ((lz ? (0xFFu << (8 - lz)) : 0u) & 0xFFu);
            while (m != 0xFF) {                             /* 3. interior runs, LONGEST FIRST */
                int L = ref_longest8(m), p = 0;
                unsigned w = (1u << L) - 1u;
                while (m & (w << p)) ++p;
                if (n < REF_MAX) { all[n].StartingIndex = k * 8 + (ULONG)p;
                                   all[n].NumberOfBits = (ULONG)L; ++n; }
                m |= (w << p) & 0xFFu;
            }
        }
        if (carry && n < REF_MAX) { all[n].StartingIndex = cstart; all[n].NumberOfBits = carry; ++n; }

        written = (n < cap) ? n : cap;
        for (i = 0; i < written; ++i) out[i] = all[i];
        return written;
    }

    /* SORTED: every run, one bit at a time, in ascending order ... */
    {
        ULONG cur = 0, start = 0;
        for (i = 0; i < bm->SizeOfBitMap; ++i) {
            ULONG bit = (bm->Buffer[i >> 5] >> (i & 31)) & 1u;
            if (bit == 0) { if (cur == 0) start = i; ++cur; }
            else if (cur) {
                if (n < REF_MAX) { all[n].StartingIndex = start; all[n].NumberOfBits = cur; ++n; }
                cur = 0;
            }
        }
        if (cur && n < REF_MAX) { all[n].StartingIndex = start; all[n].NumberOfBits = cur; ++n; }
    }

    /* ... and a STABLE insertion sort by descending length: stability IS the tie-break */
    for (i = 1; i < n; ++i) {
        REF_RUN key = all[i];
        ULONG j = i;
        while (j > 0 && all[j - 1].NumberOfBits < key.NumberOfBits) {
            all[j] = all[j - 1];
            --j;
        }
        all[j] = key;
    }
    written = (n < cap) ? n : cap;
    for (i = 0; i < written; ++i) out[i] = all[i];
    return written;
}
