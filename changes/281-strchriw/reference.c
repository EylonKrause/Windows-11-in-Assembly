/* changes/281-strchriw/reference.c
 *
 * THE SCALAR MODEL: shlwapi!StrChrIW written the slow obvious way -- one code unit at a time,
 * folding both sides and comparing.
 *
 * It exists so the gate is THREE-WAY. Comparing an implementation only against the live export
 * proves it matches Windows; comparing it also against a model written from the measured contract
 * proves the contract was read correctly in the first place. That distinction earned its keep on
 * this very change: the first model here was written against the belief that equality was the
 * ordinal upcase table, and the gate rejected it in 8 cases out of 140561.
 *
 * This model deliberately does NOT use the three dispatch paths impl.asm picks between. It asks one
 * question per haystack character through a single shared predicate, so a bug in the path SELECTION
 * -- a needle routed to the vector path that should have used a bitmap -- shows up as a
 * disagreement rather than being reproduced identically on both sides.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int wia_sci_match(unsigned needle, unsigned w);

const wchar_t* ref_strchriw(const wchar_t* s, wchar_t c)
{
    if (!s) return 0;                       /* measured: a null source returns null, not a fault */
    /* needle 0 is NOT special: NUL has zero collation weight and matches every other zero-weight
       code unit. probes/contract.c got this wrong because its test string had no ignorables. */
    for (; *s; ++s)
        if (wia_sci_match((unsigned short)c, (unsigned short)*s))
            return s;
    return 0;
}
