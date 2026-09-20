// changes/230-lstrcatw/reference.c
// Independent oracle for kernelbase!lstrcatW.
//
// Measured in probes/catw.c against the live export, not assumed:
//
//   * a plain element append at the destination's terminator; the result is terminated, not padded,
//     and it returns the DESTINATION. 0 mismatches over 81 x 81 length pairs and over all 65535
//     code unit values placed in both strings (131070 placements).
//   * a NULL source returns NULL and leaves the destination alone; a NULL destination returns NULL.
//   * An empty source still stores the terminator -- a PAGE_READONLY destination returns NULL for
//     lstrcatW(readonly, L""), so there is no early exit to take.
//   * Whole characters only at a page edge: compared against two explicit models across every
//     writable width, 19 odd widths matched the whole-character model and 0 matched byte-wise.
//
// The fault paths are not modelled here -- an oracle that walked off a guard page would take the
// test process down. correctness.c compares our implementation against the live export for those.
#include <wchar.h>

wchar_t* ref_lstrcatw(wchar_t* dst, const wchar_t* src)
{
    if (dst == 0 || src == 0) return 0;

    wchar_t* d = dst;
    while (*d) ++d;
    while ((*d++ = *src++) != 0) { }
    return dst;
}
