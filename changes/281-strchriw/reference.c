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
 * This model deliberately does NOT use the class-member shortcut impl.asm is built on. It folds
 * every haystack character and compares representatives, which is the definition probes/foldtable.c
 * measured. If the shortcut were ever wrong -- a fifth member appearing in a class the pool holds
 * as four -- the two would disagree and the gate would say so.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern unsigned short wia_sci_fold[65536];

const wchar_t* ref_strchriw(const wchar_t* s, wchar_t c)
{
    unsigned f;
    if (!s) return 0;                       /* measured: a null source returns null, not a fault */
    if (!c) return 0;                       /* measured: the terminator is never found */
    f = wia_sci_fold[(unsigned short)c];
    for (; *s; ++s)
        if (wia_sci_fold[(unsigned short)*s] == f)
            return s;
    return 0;
}
