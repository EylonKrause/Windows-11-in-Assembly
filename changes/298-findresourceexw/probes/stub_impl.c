/* probes/stub_impl.c -- a stand-in for impl.asm, used only to prove that
 * reference.c matches the LIVE export before a line of assembly was written
 * (docs/METHODOLOGY.md step 4).  It satisfies the two symbols correctness.c
 * imports by forwarding to the reference.  Build:
 *
 *   cl /O2 /I.. ..\correctness.c ..\reference.c stub_impl.c ntdll.lib /Fe:refcheck.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern HRSRC    ref_findresourceexw(HMODULE, const wchar_t*, const wchar_t*, WORD);
extern wchar_t* ref_resname_upcase(wchar_t* dst, const wchar_t* src);

HRSRC wia_findresourceexw(HMODULE m, const wchar_t* t, const wchar_t* n, WORD l)
{
    return ref_findresourceexw(m, t, n, l);
}

INT64 wia_resname_upcase(wchar_t* dst, SIZE_T limit_chars, const wchar_t* src)
{
    size_t len = 0;
    while (src[len]) ++len;
    if (len >= limit_chars) return -1;
    ref_resname_upcase(dst, src);
    return (INT64)len;
}
