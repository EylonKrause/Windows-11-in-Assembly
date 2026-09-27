// changes/300-rtlsetallbits/reference.c
// Scalar oracle for ntdll!RtlSetAllBits, derived from probes/contract.c rather than assumed.
//
// The whole contract is one line, and the only part that was in doubt is the tail. The probe
// framed the buffer in 0xA5 poison and read back the first and last modified byte for every
// SizeOfBitMap from 0 to 1024, which settles it:
//
//   * the fill is in whole ULONGs -- ((N + 31) / 32) * 4 bytes -- never ceil(N/8);
//   * the padding bits past N inside that final ULONG are SET, not preserved;
//     SizeOfBitMap=1 writes four bytes of FF, not one byte of 01;
//   * SizeOfBitMap=0 writes NOTHING, so Buffer is never dereferenced;
//   * RtlClearAllBits is the same function with 0x00, and shares the byte count exactly.
//
// So the routine is memset(Buffer, 0xFF, ((N + 31) / 32) * 4) and nothing else. The byte count is
// computed in 64 bits here deliberately: N is a ULONG, and N + 31 overflows a 32-bit register for
// N > 0xFFFFFFE0. probes/contract.c covers that boundary under SEH rather than leaving it to a
// comment.
#include <string.h>

void ref_setallbits(unsigned long size_of_bitmap, unsigned long* buffer) {
    unsigned long long bytes = (((unsigned long long)size_of_bitmap + 31ull) / 32ull) * 4ull;
    if (bytes) memset(buffer, 0xFF, (size_t)bytes);
}
