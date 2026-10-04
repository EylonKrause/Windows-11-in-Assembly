/* changes/309-chartooem/reference.c
 * Scalar oracle for the user32 OEM converters on a single-byte OEM code page, one unit or byte at a
 * time through the documented APIs the exports call -- WideCharToMultiByte(CP_OEMCP, 0, ..., "_") and
 * MultiByteToWideChar(CP_OEMCP, MB_PRECOMPOSED | MB_USEGLYPHCHARS) -- in the export's order: read
 * unit i, write byte i. Counts the export rejects are not modelled (impl.asm hands them over).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static char c2o1(wchar_t w) { char o = 0; WideCharToMultiByte(CP_OEMCP, 0, &w, 1, &o, 1, "_", NULL); return o; }
static wchar_t o2c1(char b) { wchar_t w = 0; MultiByteToWideChar(CP_OEMCP, MB_PRECOMPOSED | MB_USEGLYPHCHARS, &b, 1, &w, 1); return w; }

BOOL ref_c2ob(LPCWSTR s, LPSTR d, DWORD n) {
    if (!s || !d || (const void*)s == (const void*)d) return FALSE;
    for (DWORD i = 0; i < n; ++i) d[i] = c2o1(s[i]);
    return TRUE;
}
BOOL ref_c2o(LPCWSTR s, LPSTR d) {
    if (!s || !d || (const void*)s == (const void*)d) return FALSE;
    DWORD n = 0; while (s[n]) ++n;
    for (DWORD i = 0; i <= n; ++i) d[i] = c2o1(s[i]);
    return TRUE;
}
BOOL ref_o2cb(LPCSTR s, LPWSTR d, DWORD n) {
    if (!s || !d || (const void*)s == (const void*)d || n == 0) return FALSE;
    for (DWORD i = 0; i < n; ++i) d[i] = o2c1(s[i]);
    return TRUE;
}
BOOL ref_o2c(LPCSTR s, LPWSTR d) {
    if (!s || !d || (const void*)s == (const void*)d) return FALSE;
    DWORD n = 0; while (s[n]) ++n;
    for (DWORD i = 0; i <= n; ++i) d[i] = o2c1(s[i]);
    return TRUE;
}
