// changes/227-lstrcpya/reference.c
// Independent oracle for kernelbase!lstrcpyA.
//
// Everything below was measured in probes/cpya.c against the live export, not assumed:
//
//   * a plain BYTE COPY, terminator included. All 255 non-NUL byte values at three positions, and
//     64 start alignments x lengths 0..300 compared WHOLE-BUFFER against memcpy: 0 disagreements.
//     ACP 1252 has ZERO DBCS lead bytes (GetCPInfo), so there is no MBCS rule to respect.
//   * the destination is TERMINATED, NOT PADDED -- the bytes past the terminator are left alone,
//     which is why every test compares the whole buffer against a poison fill.
//   * it returns the DESTINATION on success.
//   * a NULL source returns NULL and LEAVES THE DESTINATION ALONE; a NULL destination returns
//     NULL; both NULL returns NULL.
//
// THE FAULT PATHS ARE NOT MODELLED HERE, deliberately. An oracle that walked off a guard page
// would take the test process down, and the behaviour to match is not a value this function can
// compute -- it is "return NULL, and leave in the destination exactly the bytes that were
// transferable". correctness.c handles those cases by comparing OUR implementation against the
// LIVE EXPORT directly, byte for byte, which is the only honest comparison available there.

char* ref_lstrcpya(char* dst, const char* src)
{
    if (dst == 0 || src == 0) return 0;

    char* d = dst;
    while ((*d++ = *src++) != 0) { }
    return dst;
}
