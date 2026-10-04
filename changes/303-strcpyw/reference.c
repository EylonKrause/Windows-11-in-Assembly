/* changes/303-strcpyw/reference.c
 * Scalar oracle for shlwapi!StrCpyW / StrCatW, as discovery/strcpyw_contract.c measured them: a NULL
 * argument returns the destination untouched; the copy moves one character at a time and stops after
 * the terminator, so every readable character is written before an unreadable one faults; StrCatW
 * finds the destination's end before it writes anything.
 */
#include <wchar.h>

wchar_t* ref_strcpyw(wchar_t* d, const wchar_t* s) {
    if (!d || !s) return d;
    wchar_t* p = d;
    while ((*p++ = *s++) != 0) {}
    return d;
}

wchar_t* ref_strcatw(wchar_t* d, const wchar_t* s) {
    if (!d || !s) return d;
    wchar_t* p = d;
    while (*p) ++p;
    while ((*p++ = *s++) != 0) {}
    return d;
}
