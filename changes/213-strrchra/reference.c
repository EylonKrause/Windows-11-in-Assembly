// changes/213-strrchra/reference.c
// Oracle for shlwapi!StrRChrA. Not fast; just obviously right.
//
// The contract, measured in probes/srca.c against the live export:
//   * byte-wise on this code page -- 0 of 255 byte values act as a DBCS lead byte (ACP is 1252);
//   * pszEnd == NULL means "to the terminator"; otherwise the range is [pszStart, pszEnd),
//     EXCLUSIVE -- an pszEnd sitting on a match finds the PREVIOUS one;
//   * only the LOW BYTE of wMatch is consulted (0x015A, 0x5A5A and 0xFF5A all find 'Z');
//   * a low byte of 0 -- searching for the terminator -- always returns NULL, because a valid range
//     stops at or before the NUL and so never contains one;
//   * pszStart == NULL returns NULL; an empty range returns NULL.
//
// DOMAIN. The shipped export walks FORWARD with CharNextA, which does not advance past a
// terminator, so an pszEnd placed beyond the string's NUL makes it spin forever -- measured twice.
// That is out of contract and a hang is not behaviour a caller can depend on, so this oracle simply
// answers, and correctness.c never leaves the domain.
#include <windows.h>

const char* ref_strrchra(const char* start, const char* end, WORD match)
{
    const char* last = 0;
    const char* p;
    char m = (char)(match & 0xFF);

    if (!start) return 0;
    if (m == 0) return 0;                      /* a valid range can never hold a NUL */

    if (!end) { end = start; while (*end) ++end; }
    if (end <= start) return 0;

    for (p = start; p < end; ++p)
        if (*p == m) last = p;

    return last;
}
