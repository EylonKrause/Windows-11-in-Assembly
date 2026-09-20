/* changes/261-rtlfindnextforwardrunclear/reference.c
 *
 * The independent oracle for RtlFindNextForwardRunClear and RtlFindLastBackwardRunClear.
 *
 * It shares nothing with impl.asm but the contract. impl.asm skips thirty-two bytes at a time with
 * VPCMPEQD and finds the ends with TZCNT and LZCNT; this looks at one bit, then the next.
 *
 * THE RULES, as probes/contract.c measured them:
 *
 *   * Both forms clip at FromIndex, in opposite directions. Forward finds the first clear bit at or
 *     after FromIndex and reports the run FROM THERE -- asked from 105 inside a run of 100..119 it
 *     answers start=105, length=15, not start=100, length=20. Backward finds the last clear bit at
 *     or before FromIndex and reports the run from its TRUE START to that bit -- asked back from 105
 *     it answers start=100, length=6.
 *   * FromIndex IS INCLUDED in both.
 *   * nothing FOUND writes the start pointer anyway, and with different values: the forward form
 *     writes SizeOfBitMap, the backward form writes 0.
 *   * FromIndex at or past SizeOfBitMap returns 0 and writes FromIndex itself -- not the size, and
 *     not zero. That is the one case where the two forms agree.
 *   * The slack past SizeOfBitMap never extends a run: the same buffer with bits 1000..1023 clear
 *     answers 24 when declared as 1024 bits and 10 when declared as 1010.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } REF_RBM;

static ULONG ref_bit(const ULONG* b, ULONG i) { return (b[i >> 5] >> (i & 31)) & 1u; }

ULONG ref_findnextforwardrunclear(void* bmv, ULONG from, PULONG start)
{
    REF_RBM* bm = (REF_RBM*)bmv;
    ULONG i, s, e;
    if (from >= bm->SizeOfBitMap) { *start = from; return 0; }
    for (i = from; i < bm->SizeOfBitMap; ++i)
        if (ref_bit(bm->Buffer, i) == 0) break;
    if (i >= bm->SizeOfBitMap) { *start = bm->SizeOfBitMap; return 0; }
    s = i;
    for (e = s; e < bm->SizeOfBitMap; ++e)
        if (ref_bit(bm->Buffer, e) != 0) break;
    *start = s;
    return e - s;
}

ULONG ref_findlastbackwardrunclear(void* bmv, ULONG from, PULONG start)
{
    REF_RBM* bm = (REF_RBM*)bmv;
    LONG i;
    ULONG s, e;
    if (from >= bm->SizeOfBitMap) { *start = from; return 0; }
    for (i = (LONG)from; i >= 0; --i)
        if (ref_bit(bm->Buffer, (ULONG)i) == 0) break;
    if (i < 0) { *start = 0; return 0; }
    e = (ULONG)i;                                 /* the last clear bit at or before FromIndex */
    s = e;
    while (s > 0 && ref_bit(bm->Buffer, s - 1) == 0) --s;
    *start = s;
    return e - s + 1;
}
