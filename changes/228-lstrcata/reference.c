// changes/228-lstrcata/reference.c
// Independent oracle for kernelbase!lstrcatA.
//
// Measured in probes/cata.c against the live export, not assumed:
//
//   * a plain byte append: the source is copied, terminator included, at the destination's
//     terminator. All 255 non-NUL byte values in BOTH strings (510 placements) and every
//     destination length 0..120 against every source length 0..120, compared WHOLE-BUFFER: 0
//     disagreements. ACP 1252 has ZERO DBCS lead bytes (GetCPInfo).
//   * the result is TERMINATED, NOT PADDED.
//   * it returns the DESTINATION on success.
//   * AN EMPTY SOURCE STILL STORES THE TERMINATOR. Appending "" leaves the buffer byte-for-byte
//     identical, which looks like "writes nothing" -- but writing a 0 over a 0 is indistinguishable
//     from not writing. A PAGE_READONLY destination settles it: lstrcatA(readonly, "") returns
//     NULL, so the store happens. No early exit.
//   * a NULL source returns NULL and leaves the destination alone; a NULL destination returns NULL.
//
// THE FAULT PATHS ARE NOT MODELLED HERE, deliberately: an oracle that walked off a guard page would
// take the test process down, and the behaviour to match is not a value this function can compute.
// correctness.c handles those by comparing OUR implementation against the LIVE EXPORT directly.

char* ref_lstrcata(char* dst, const char* src)
{
    if (dst == 0 || src == 0) return 0;

    char* d = dst;
    while (*d) ++d;
    while ((*d++ = *src++) != 0) { }
    return dst;
}
