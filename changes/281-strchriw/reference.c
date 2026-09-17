/* changes/281-strchriw/reference.c
 *
 * THE SCALAR MODEL: shlwapi!StrChrIW written the slow obvious way -- one code unit at a time,
 * upcasing both sides through the table and comparing.
 *
 * It exists so the gate is THREE-WAY. Comparing an implementation only against the live export
 * proves it matches Windows; comparing it also against a model written from the measured contract
 * proves the contract was read correctly in the first place. Change 269's gate agreed with live on
 * every case and was still blind, twice over.
 *
 * This model deliberately does NOT use the {U, downcase(U)} shortcut that impl.asm is built on. It
 * upcases every haystack character and compares, which is the definition probes/contract.c
 * measured. If the shortcut were ever wrong -- a third member appearing in some equivalence class,
 * which tables.c also checks for directly -- the two would disagree and the gate would say so.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern unsigned short wia_sci_up[65536];

const wchar_t* ref_strchriw(const wchar_t* s, wchar_t c)
{
    unsigned u;
    if (!s) return 0;                       /* measured: a null source returns null, not a fault */
    if (!c) return 0;                       /* measured: the terminator is never found */
    u = wia_sci_up[(unsigned short)c];
    for (; *s; ++s)
        if (wia_sci_up[(unsigned short)*s] == u)
            return s;
    return 0;
}
