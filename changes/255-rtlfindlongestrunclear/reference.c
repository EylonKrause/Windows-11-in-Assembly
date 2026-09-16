/* changes/255-rtlfindlongestrunclear/reference.c
 *
 * THE INDEPENDENT ORACLE for RtlFindLongestRunClear.
 *
 * It shares nothing with impl.asm but the contract. impl.asm reads sixty-four bits at a time, finds
 * the longest run inside a word without looping over its runs (`x &= x >> 1` until empty), skips
 * whole words with a POPCNT bound, and carries a partial run across word boundaries. This walks the
 * bitmap ONE BIT AT A TIME.
 *
 * That is the right shape for an oracle here for a specific reason: every hard part of the
 * implementation is a boundary -- the carry between words, the masking of the slack past
 * SizeOfBitMap, the odd trailing ULONG, the tie-break between equal runs. A bit-at-a-time loop has
 * none of those boundaries to get wrong. It is also, by construction, the same algorithm the
 * shipped code appears to use, which is a point in its favour as a cross-check and not a problem:
 * it is never benchmarked, only believed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } REF_RBM;

ULONG ref_findlongestrunclear(void* bmv, ULONG* StartingIndex)
{
    REF_RBM* bm = (REF_RBM*)bmv;
    ULONG n, i, best = 0, best_start = 0, cur = 0, cur_start = 0;

    if (!bm || !bm->Buffer || bm->SizeOfBitMap == 0) {
        if (StartingIndex) *StartingIndex = 0;
        return 0;
    }
    n = bm->SizeOfBitMap;

    for (i = 0; i < n; ++i) {
        ULONG bit = (bm->Buffer[i >> 5] >> (i & 31)) & 1u;
        if (bit == 0) {
            if (cur == 0) cur_start = i;
            ++cur;
            if (cur > best) {            /* STRICTLY greater: the FIRST run wins a tie */
                best = cur;
                best_start = cur_start;
            }
        } else {
            cur = 0;
        }
    }
    if (StartingIndex) *StartingIndex = best_start;
    return best;
}
