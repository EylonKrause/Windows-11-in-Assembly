/* changes/302-charupperw/reference.c
 *
 * Scalar oracle for user32!CharUpperW / CharLowerW, written against NTDLL rather than user32 so it is
 * independent of the tables impl.asm uses: discovery/charupperw_string.c established that both modes
 * agree with RtlUpcaseUnicodeChar / RtlDowncaseUnicodeChar on every code unit (0 of 65535 in string
 * mode, 0 of 65536 in character mode), that the mode test is (value >> 16) == 0 over all 64 bits, and
 * that the length is found BEFORE anything is written.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

typedef WCHAR (NTAPI *PFN_RC)(WCHAR);
static PFN_RC up, dn;

static void init(void) {
    if (!up) {
        HMODULE n = GetModuleHandleW(L"ntdll.dll");
        up = (PFN_RC)GetProcAddress(n, "RtlUpcaseUnicodeChar");
        dn = (PFN_RC)GetProcAddress(n, "RtlDowncaseUnicodeChar");
    }
}

static LPWSTR map(LPWSTR p, PFN_RC f) {
    if (((UINT_PTR)p >> 16) == 0) return (LPWSTR)(UINT_PTR)f((WCHAR)(UINT_PTR)p);
    size_t n = wcslen(p);                        /* first the length ...                      */
    for (size_t i = 0; i < n; ++i) p[i] = f(p[i]);   /* ... then the mapping, never past the NUL */
    return p;
}

LPWSTR ref_charupperw(LPWSTR p) { init(); return map(p, up); }
LPWSTR ref_charlowerw(LPWSTR p) { init(); return map(p, dn); }
