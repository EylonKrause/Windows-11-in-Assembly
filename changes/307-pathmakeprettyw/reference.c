/* changes/307-pathmakeprettyw/reference.c
 * Scalar oracle for shlwapi!PathMakePrettyW, as the disassembly and
 * discovery/pathmakeprettyw_contract.c establish it: refuse on 'a'..'z' anywhere (unbounded) writing
 * nothing; lowercase units [0, min(len, 259)) -- per unit, plus the Deseret pair U+10400..U+10427 ->
 * +0x28 -- writing every one of them; cut a path of 260+ units at 259; uppercase unit 0 alone; return 1.
 * The per-unit maps are LCMapStringW's, called directly here (tables.c builds the same thing for the
 * assembly).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static wchar_t lower1(wchar_t c) { wchar_t d = c; LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE, &c, 1, &d, 1); return d; }
static wchar_t upper1(wchar_t c) { wchar_t d = c; LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_UPPERCASE, &c, 1, &d, 1); return d; }

BOOL ref_pathmakeprettyw(wchar_t* p) {
    if (!p) return FALSE;
    size_t len = 0;
    for (; p[len]; ++len)
        if (p[len] >= L'a' && p[len] <= L'z') return FALSE;
    size_t m = len < 259 ? len : 259;
    for (size_t i = 0; i < m; ++i) {
        wchar_t c = p[i];
        if (c == 0xD801 && i + 1 < m && p[i + 1] >= 0xDC00 && p[i + 1] <= 0xDC27) { p[i + 1] += 0x28; ++i; continue; }
        p[i] = lower1(c);
    }
    if (len >= 260) p[259] = 0;
    p[0] = upper1(p[0]);
    return TRUE;
}
