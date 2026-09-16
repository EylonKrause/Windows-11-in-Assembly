/* changes/257-rtlnumberofsetbits/reference.c
 *
 * THE INDEPENDENT ORACLE for the RtlNumberOfSetBits family.
 *
 * It shares nothing with impl.asm but the contract. impl.asm counts thirty-two bytes at a time with
 * a VPSHUFB nibble table and VPSADBW, masks two partial words at the ends, and assembles those
 * partial words from bounds-checked 32-bit reads. This adds up ONE BIT AT A TIME.
 *
 * That is the right shape for an oracle here because everything difficult in the implementation is
 * an EDGE -- the mask below the start, the mask at the end, the partial word that must not be read
 * as sixty-four bits, the seam between the vector body and the scalar remainder -- and a
 * bit-at-a-time loop has none of them.
 *
 * The refusal predicate is reproduced exactly as probes/contract.c measured it: the range forms
 * REFUSE rather than clamp, returning 0xFFFFFFFF for a zero length or a range that runs past
 * SizeOfBitMap.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } REF_RBM;

static ULONG ref_count(REF_RBM* bm, ULONG from, ULONG to)
{
    ULONG i, n = 0;
    for (i = from; i < to; ++i)
        n += (bm->Buffer[i >> 5] >> (i & 31)) & 1u;
    return n;
}

ULONG ref_numberofsetbits(void* bmv)
{
    REF_RBM* bm = (REF_RBM*)bmv;
    if (!bm || !bm->Buffer || bm->SizeOfBitMap == 0) return 0;
    return ref_count(bm, 0, bm->SizeOfBitMap);
}

ULONG ref_numberofclearbits(void* bmv)
{
    REF_RBM* bm = (REF_RBM*)bmv;
    if (!bm || !bm->Buffer || bm->SizeOfBitMap == 0) return 0;
    return bm->SizeOfBitMap - ref_count(bm, 0, bm->SizeOfBitMap);
}

ULONG ref_numberofsetbitsinrange(void* bmv, ULONG start, ULONG len)
{
    REF_RBM* bm = (REF_RBM*)bmv;
    if (!bm) return 0xFFFFFFFFu;
    if (len == 0) return 0xFFFFFFFFu;                       /* refuses, does not clamp */
    if (start + len < start) return 0xFFFFFFFFu;            /* overflowed */
    if (start + len > bm->SizeOfBitMap) return 0xFFFFFFFFu;
    if (!bm->Buffer) return 0xFFFFFFFFu;
    return ref_count(bm, start, start + len);
}

ULONG ref_numberofclearbitsinrange(void* bmv, ULONG start, ULONG len)
{
    ULONG r = ref_numberofsetbitsinrange(bmv, start, len);
    if (r == 0xFFFFFFFFu) return r;
    return len - r;
}
