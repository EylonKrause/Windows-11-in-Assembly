/* changes/256-rtlfindsetbits/reference.c
 *
 * The independent oracle for RtlFindSetBits and RtlFindClearBits.
 *
 * It shares nothing with impl.asm but the contract. impl.asm reads sixty-four bits at a time,
 * inverts the word for the clear-bit search, masks both ends, carries a partial run across word
 * boundaries, and answers "is there a run of N inside this word" with a bounded shift-and loop.
 * This walks the bitmap one bit at a time, twice.
 *
 * That is the right shape for an oracle here because every hard part of the implementation is a
 * BOUNDARY -- the wrap between the two passes, the carry between words, the masking below the hint
 * and past SizeOfBitMap, the odd trailing ULONG -- and a bit-at-a-time loop has none of them.
 *
 * The two passes are written out explicitly, because the wrap is the part of the contract that is
 * easiest to get subtly wrong: probes/contract.c measured that the search covers [hint, size) and
 * then starts again from the beginning, and that a run STRADDLING the wrap point does NOT count --
 * the bitmap is not circular, only the search order is.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } REF_RBM;

static ULONG ref_scan(REF_RBM* bm, ULONG want, ULONG from, int set)
{
    ULONG i, cur = 0, start = 0;
    for (i = from; i < bm->SizeOfBitMap; ++i) {
        ULONG bit = (bm->Buffer[i >> 5] >> (i & 31)) & 1u;
        if (bit == (ULONG)(set ? 1 : 0)) {
            if (cur == 0) start = i;
            ++cur;
            if (cur >= want) return start;
        } else {
            cur = 0;
        }
    }
    return 0xFFFFFFFFu;
}

static ULONG ref_find(void* bmv, ULONG want, ULONG hint, int set)
{
    REF_RBM* bm = (REF_RBM*)bmv;
    ULONG r;
    if (!bm) return 0xFFFFFFFFu;
    /* NumberToFind = 0 returns the hint rounded down to a multiple of eight -- not 0, which is only
       what it looks like when the hint happens to be under 8. See probes/zeron.c. */
    if (want == 0)
        return ((hint < bm->SizeOfBitMap) ? hint : 0u) & 0xFFFFFFF8u;
    if (!bm->Buffer) return 0xFFFFFFFFu;
    if (want > bm->SizeOfBitMap) return 0xFFFFFFFFu;
    if (hint >= bm->SizeOfBitMap) hint = 0;        /* at or past the end: treated as zero */

    r = ref_scan(bm, want, hint, set);             /* pass 1: [hint, size) */
    if (r != 0xFFFFFFFFu) return r;
    if (hint == 0) return 0xFFFFFFFFu;             /* pass 1 already covered everything */
    return ref_scan(bm, want, 0, set);             /* pass 2: from the beginning */
}

ULONG ref_findsetbits(void* bm, ULONG want, ULONG hint)   { return ref_find(bm, want, hint, 1); }
ULONG ref_findclearbits(void* bm, ULONG want, ULONG hint) { return ref_find(bm, want, hint, 0); }
