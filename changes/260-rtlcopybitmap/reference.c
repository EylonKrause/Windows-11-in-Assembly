/* changes/260-rtlcopybitmap/reference.c
 *
 * THE INDEPENDENT ORACLE for RtlCopyBitMap and RtlExtractBitMap.
 *
 * It shares nothing with impl.asm but the contract. impl.asm moves 256 bits at a time with a
 * funnel shift built from VPSRLQ/VPSLLQ/VPOR, and merges a masked word at each end; this copies one
 * bit at a time. There is no shift to get wrong here, no word boundary and no vector tail.
 *
 * THE RULES, as probes/contract.c measured them rather than as the documentation states them:
 *
 *   * COPY reads the source from BIT 0 and writes it AT TargetBit.
 *     EXTRACT reads the source AT TargetBit and writes it from BIT 0. They are the same move in
 *     opposite directions.
 *
 *   * RtlCopyBitMap's FOURTH ARGUMENT IS IGNORED. It is a three-argument function -- r9d is
 *     overwritten at RVA 0x13E34A before it is ever read -- and passing 0, 1, 16 or 0xFFFFFFFF as a
 *     fourth argument produces byte-for-byte identical results. The count is
 *
 *           min(Source->SizeOfBitMap, Destination->SizeOfBitMap - TargetBit)
 *
 *     and RtlExtractBitMap's, which does take four, is
 *
 *           min(NumberOfBits, Source->SizeOfBitMap - TargetBit, Destination->SizeOfBitMap)
 *
 *   * THAT SUBTRACTION IS DONE IN 32 BITS AND TESTED IN 64, so a TargetBit PAST the destination's
 *     size does not refuse -- it wraps to a huge unsigned count and copies the whole source anyway,
 *     past the declared size. With a 64-bit destination, TargetBit = 64 copies nothing and
 *     TargetBit = 65 writes four bytes at byte 8. That is reproduced here deliberately: it is what
 *     the shipped export does, and an implementation that "fixed" it would not be a replacement.
 *
 *   * EVERY BIT OUTSIDE THE RANGE IS PRESERVED, in both directions -- copying five bits into bits
 *     3..7 of a destination byte holding 0xCC leaves 0xC4, not 0x18.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } REF_RBM;

static ULONG ref_get(const ULONG* b, ULONG i) { return (b[i >> 5] >> (i & 31)) & 1u; }
static void  ref_put(ULONG* b, ULONG i, ULONG v)
{
    if (v) b[i >> 5] |=  (1u << (i & 31));
    else   b[i >> 5] &= ~(1u << (i & 31));
}

VOID ref_copybitmap(void* sv, void* dv, ULONG target)
{
    REF_RBM* s = (REF_RBM*)sv;
    REF_RBM* d = (REF_RBM*)dv;
    ULONG count, i;
    if (!s || !d || !s->Buffer || !d->Buffer) return;
    count = d->SizeOfBitMap - target;            /* 32-bit, and deliberately allowed to wrap */
    if (s->SizeOfBitMap <= count) count = s->SizeOfBitMap;
    for (i = 0; i < count; ++i)
        ref_put(d->Buffer, target + i, ref_get(s->Buffer, i));
}

VOID ref_extractbitmap(void* sv, void* dv, ULONG target, ULONG nbits)
{
    REF_RBM* s = (REF_RBM*)sv;
    REF_RBM* d = (REF_RBM*)dv;
    ULONG count, i;
    if (!s || !d || !s->Buffer || !d->Buffer) return;
    count = s->SizeOfBitMap - target;            /* 32-bit, and deliberately allowed to wrap */
    if (nbits <= count) count = nbits;
    if (count > d->SizeOfBitMap) count = d->SizeOfBitMap;
    for (i = 0; i < count; ++i)
        ref_put(d->Buffer, i, ref_get(s->Buffer, target + i));
}
