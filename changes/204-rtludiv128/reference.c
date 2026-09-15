// changes/204-rtludiv128/reference.c
// The correctness oracle for ntdll!RtlUdiv128. Not fast; just obviously right.
//
// This is a DIRECT TRANSCRIPTION of the shipped algorithm (RVA 0x0014A250) -- a 64-iteration
// restoring shift-subtract long division. The implementation's claim is that a single hardware `div`
// reproduces it exactly wherever the quotient is representable (DividendHigh < Divisor), and that
// claim is only worth something if the oracle is the loop rather than a restatement of the shortcut.
//
// probes/udiv.c confirmed this transcription against the live export over 1728 structured triples
// and 400 000 fuzz cases weighted onto the overflow boundary: 0 mismatches.
//
// Contract, every line of it measured:
//   * rcx = dividend high, rdx = dividend low, r8 = divisor, r9 = ULONG64* remainder;
//   * the quotient accumulates ONE BIT PER ITERATION into a 64-bit register;
//   * when DividendHigh >= Divisor the quotient cannot be represented, and what comes back has NO
//     CLOSED FORM. Two were tried and both are wrong: "saturates to all-ones with remainder
//     lo + d" matches (1, 0, 1) -> FFFFFFFFFFFFFFFF r 1 but not (7FFFFFFFFFFFFFFF, 0, 100000000h)
//     -> FFFFFFFF00000000 r 0; "the true quotient mod 2^64" matches the second and not the first.
//     The reason is that this loop's 64-bit remainder register overflows -- `sar 63` recovers one
//     lost bit for the comparison, but bits already shifted off the top of r are gone. So the loop
//     itself is the specification for that region, which is exactly why this oracle is the loop;
//   * Divisor == 0 is not special-cased by the shipped code and does not fault: it simply lands in
//     that same always-subtract region, returning all-ones with Remainder = DividendLow;
//   * the remainder pointer may be NULL, and is tested before the store.
#include <windows.h>

unsigned __int64 ref_udiv128(unsigned __int64 hi, unsigned __int64 lo,
                             unsigned __int64 d,  unsigned __int64* rem)
{
    unsigned __int64 r = hi, q = lo;

    for (int i = 0; i < 64; ++i) {
        unsigned __int64 shifted_out = r >> 63;            /* the bit leaving the top of r  */
        unsigned __int64 cand        = (r << 1) | (q >> 63);
        unsigned __int64 q_set       = (q << 1) | 1;
        /* The shipped code ORs in (hi sar 63) before comparing, which saturates the test value to
           all-ones whenever a bit was shifted out of the remainder -- so such an iteration always
           subtracts, even though the 64-bit `cand` may look small. */
        unsigned __int64 test        = shifted_out ? ~(unsigned __int64)0 : cand;

        if (test >= d) { r = cand - d;  q = q_set;     }
        else           { r = cand;      q = (q << 1);  }
    }

    if (rem) *rem = r;
    return q;
}
