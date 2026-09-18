/* changes/286-strchrniw/reference.c
 *
 * THE SCALAR MODEL for shlwapi!StrChrNIW, from the contract measured in probes/contract.c:
 *
 *     PWSTR StrChrNIW(PCWSTR start, WCHAR match, UINT cchMax)
 *
 *   * IT TAKES A COUNT, NOT AN END POINTER, and that had to be settled rather than read off a header:
 *     discovery/charclass_strcmp_2026.c timed it as (start, start+511, char) -- reusing StrRChrIW's
 *     three-argument typedef and labelling it "range form" -- which does not fault, so it produced a
 *     number that was not this function's cost. Called that way the export returns NULL; called as
 *     (start, char, count) it returns the right pointer;
 *   * the count is the number of characters EXAMINED: indices 0 .. cchMax-1. A count of 2 does not
 *     reach index 2, a count of 3 does, and a count of 0 gives NULL;
 *   * the relation is change 281's -- the intransitive triple holds, it is symmetric, the 3237-member
 *     ignorable set works, and U+200B matches only itself;
 *   * THE TERMINATOR STOPS THE SCAN AND IS NEVER A MATCH. This is where it differs from changes 283 and
 *     284: there a needle character that matches a NUL matched the terminator itself. Here searching for
 *     a NUL, or for a SOFT HYPHEN, over "abcd" gives NULL. An embedded NUL stops it the same way;
 *   * a NULL start gives NULL.
 *
 * One thing the export does that this model deliberately does not: on an UNTERMINATED string it faults
 * even when the count covers the whole buffer, so the count does not bound its reads. A caller must
 * supply a terminator. The model reads only indices 0..cchMax-1 and stops at the terminator, which
 * agrees with the export on every input the export survives.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int wia_sci_match(unsigned needle, unsigned w);

const wchar_t* ref_strchrniw(const wchar_t* s, wchar_t match, unsigned cchMax)
{
    unsigned i;
    if (!s) return 0;
    for (i = 0; i < cchMax; ++i) {
        unsigned c = (unsigned short)s[i];
        if (!c) return 0;                       /* the terminator: stops it, and never matches */
        if (wia_sci_match((unsigned short)match, c)) return s + i;
    }
    return 0;
}
