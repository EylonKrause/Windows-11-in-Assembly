/* changes/262-rtlfindsetbitsandclear/reference.c
 *
 * THE INDEPENDENT ORACLE for RtlFindSetBitsAndClear and RtlFindClearBitsAndSet.
 *
 * It shares nothing with impl.asm but the contract. impl.asm reuses change 256's aligned-block
 * filter -- which rejects thirty-two bytes with one compare and rebuilds a run only around a
 * witness -- and then writes the range with masked ends and a vector middle. This counts bits one
 * at a time and writes them one at a time.
 *
 * THE RULES, as probes/contract.c measured them:
 *
 *   * THE SEARCH WRAPS. It scans [hint, size) and then starts again from the beginning; the run
 *     AFTER the hint wins if there is one.
 *   * A RUN STRADDLING THE WRAP POINT DOES NOT COUNT. The bitmap is not circular, only the order.
 *   * A HINT AT OR PAST SizeOfBitMap is treated as zero.
 *   * NumberToFind = 0 returns the hint rounded DOWN to a multiple of eight (or 0 when the hint is
 *     at or past the size) and WRITES NOTHING.
 *   * NumberToFind > SizeOfBitMap is not found, and the slack past SizeOfBitMap never contributes.
 *   * EXACTLY NumberToFind bits are written, at the returned index -- not the whole run that was
 *     found. Asking for 8 inside a run of 20 leaves the other 12 alone.
 *   * NOT FOUND writes nothing at all.
 *
 * The second pass is written here as [0, size) rather than [0, hint): if the first pass scanned
 * [hint, size) and found nothing, no qualifying run lies entirely within it, so the two are the
 * same set of answers and the simpler one is easier to be sure of.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } REF_RBM;

static ULONG ref_bit(const ULONG* b, ULONG i) { return (b[i >> 5] >> (i & 31)) & 1u; }

/* The first index in [lo, hi) starting a run of `n` bits all equal to `want`. */
static ULONG ref_scan(const ULONG* buf, ULONG lo, ULONG hi, ULONG n, ULONG want)
{
    ULONG i, run = 0;
    if (n == 0 || hi < n) return 0xFFFFFFFFul;
    for (i = lo; i < hi; ++i) {
        if (ref_bit(buf, i) == want) {
            ++run;
            if (run >= n) return i + 1 - n;
        } else {
            run = 0;
        }
    }
    return 0xFFFFFFFFul;
}

static ULONG ref_core(void* bmv, ULONG n, ULONG hint, ULONG want)
{
    REF_RBM* bm = (REF_RBM*)bmv;
    ULONG size = bm->SizeOfBitMap, r, i;

    if (hint >= size) hint = 0;
    if (n == 0) return hint & ~7ul;            /* an index, and nothing written */
    if (n > size) return 0xFFFFFFFFul;

    r = ref_scan(bm->Buffer, hint, size, n, want);
    if (r == 0xFFFFFFFFul)
        r = ref_scan(bm->Buffer, 0, size, n, want);   /* ... and then from the beginning */
    if (r == 0xFFFFFFFFul) return r;

    for (i = 0; i < n; ++i) {                  /* exactly n bits, at exactly r */
        ULONG bit = r + i;
        if (want) bm->Buffer[bit >> 5] &= ~(1u << (bit & 31));   /* found SET   -> clear them */
        else      bm->Buffer[bit >> 5] |=  (1u << (bit & 31));   /* found CLEAR -> set them */
    }
    return r;
}

ULONG ref_findsetbitsandclear(void* bmv, ULONG n, ULONG hint) { return ref_core(bmv, n, hint, 1u); }
ULONG ref_findclearbitsandset(void* bmv, ULONG n, ULONG hint) { return ref_core(bmv, n, hint, 0u); }

/* THE NULL ANSWER, stated once so the gate does not hard-code it in two places. Change 256 answers
   NOT FOUND for a NULL RTL_BITMAP rather than faulting, and both of this change's paths -- the
   64-bit fast path and the general one that calls into 256 -- have to give the same answer. */
ULONG ref_findsetbitsandclear_null_expect(void) { return 0xFFFFFFFFul; }
