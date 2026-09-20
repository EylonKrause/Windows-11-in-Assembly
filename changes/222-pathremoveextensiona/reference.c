// changes/222-pathremoveextensiona/reference.c
// Oracle for shlwapi!PathRemoveExtensionA. Not fast; just obviously right.
//
// The contract, measured in probes/rmext.c against the live export:
//   * byte-wise on this code page, 0 of 252 byte values act as a DBCS lead byte;
//   * the extension is the last '.' after the last STOPPER, where a stopper is a BACKSLASH **or a
//     SPACE**; '/' and ':' do NOT stop the search. 0 mismatches over 335923 enumerated strings,
//     against 46158 for the rule change 140 originally shipped with;
//   * a MAX_PATH guard: 259 characters are truncated, 260 or more are left completely untouched,
//     whatever the path contains. Measured at every length from 250 to 268;
//   * it writes ONE byte, the terminator at the extension position. Nothing past it is cleared, so
//     correctness.c compares the whole buffer;
//   * NULL returns without faulting.
#include <windows.h>

void ref_pathremoveexta(char* p)
{
    int end = 0, q;

    if (!p) return;
    while (p[end]) ++end;
    if (end >= 260) return;                  /* the MAX_PATH guard */

    for (q = end; q > 0; ) {
        --q;
        if (p[q] == '.') { p[q] = 0; return; }
        if (p[q] == '\\' || p[q] == ' ') break;
    }
    /* no extension: nothing is written at all */
}
