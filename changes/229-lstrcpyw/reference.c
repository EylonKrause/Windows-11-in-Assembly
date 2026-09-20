// changes/229-lstrcpyw/reference.c
// Independent oracle for kernelbase!lstrcpyW.
//
// Measured in probes/cpyw.c against the live export, not assumed:
//
//   * a plain element copy, terminator included. All 65535 non-zero code unit values (surrogates
//     included) and 32 start alignments x lengths 0..300, whole-buffer: 0 disagreements.
//   * the destination is terminated, not padded; it returns the destination.
//   * a NULL source returns NULL and leaves the destination alone; a NULL destination returns NULL.
//   * Whole characters only at a page edge: with an odd number of writable bytes it writes
//     floor(n/2)*2 of them and never half a character.
//
// The fault paths are not modelled here -- an oracle that walked off a guard page would take the
// test process down. correctness.c compares our implementation against the live export for those.

#include <wchar.h>

wchar_t* ref_lstrcpyw(wchar_t* dst, const wchar_t* src)
{
    if (dst == 0 || src == 0) return 0;

    wchar_t* d = dst;
    while ((*d++ = *src++) != 0) { }
    return dst;
}
