/* changes/259-rtlarebitsset/reference.c
 *
 * The independent oracle for RtlAreBitsSet and RtlAreBitsClear.
 *
 * It shares nothing with impl.asm but the contract. impl.asm masks the two partial words at the
 * ends and tests the middle thirty-two bytes at a time with VPTEST; this looks at one bit, then the
 * next, and stops at the first one that disagrees. There is no mask to get wrong here, no word
 * boundary and no vector tail, which is the point of having it.
 *
 * The refusals are the part worth writing out, because none of them is what a reader would assume
 * and all three were measured rather than inferred (probes/contract.c):
 *
 *   * Length zero is FALSE. Not vacuously true.
 *   * a start at or past SizeOfBitMap is FALSE.
 *   * start + length > SizeOfBitMap IS FALSE, refused, not clamped to what fits. The slack past
 *     the declared size is out of bounds, not merely unset: an entirely-ones buffer declared as 40
 *     bits answers false to (0,41).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } REF_RBM;

static int ref_are(void* bmv, ULONG start, ULONG len, int want)
{
    REF_RBM* bm = (REF_RBM*)bmv;
    ULONG i;
    if (!bm || !bm->Buffer) return 0;
    if (start >= bm->SizeOfBitMap) return 0;          /* a start at or past the end */
    if (len == 0) return 0;                           /* length zero is REFUSED */
    if (bm->SizeOfBitMap - start < len) return 0;     /* past the end: REFUSED, not clamped */
    for (i = start; i < start + len; ++i) {
        ULONG bit = (bm->Buffer[i >> 5] >> (i & 31)) & 1u;
        if ((int)bit != want) return 0;
    }
    return 1;
}

BOOLEAN ref_arebitsset(void* bm, ULONG start, ULONG len)   { return (BOOLEAN)ref_are(bm, start, len, 1); }
BOOLEAN ref_arebitsclear(void* bm, ULONG start, ULONG len) { return (BOOLEAN)ref_are(bm, start, len, 0); }
