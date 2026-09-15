// changes/220-strchra/reference.c
// Oracle for shlwapi!StrChrA. Not fast; just obviously right.
//
// The contract, measured in probes/chr.c against the live export:
//   * byte-wise on this code page -- 0 of 254 byte values act as a DBCS lead byte (ACP is 1252);
//   * returns the first occurrence of (char)wMatch, or NULL;
//   * only the LOW BYTE of wMatch is consulted (0x015A, 0x5A5A and 0xFF5A all find 'Z');
//   * a low byte of 0 -- searching for the terminator -- returns NULL;
//   * a NULL pointer returns NULL, and so does an empty string;
//   * the scan STOPS at the terminator; every byte value 0x01..0xFF is findable, including
//     0x80..0xFF which are ordinary characters here. There is no length cap.
#include <windows.h>

const char* ref_strchra(const char* s, WORD match)
{
    char m = (char)(match & 0xFF);
    int i;

    if (!s) return 0;
    if (m == 0) return 0;

    for (i = 0; s[i]; ++i)
        if (s[i] == m) return s + i;

    return 0;
}
