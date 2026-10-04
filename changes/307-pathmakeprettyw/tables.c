/* changes/307-pathmakeprettyw/tables.c
 * The two case maps PathMakePrettyW applies, built from the API it calls: LCMapStringW with
 * LOCALE_SYSTEM_DEFAULT and LCMAP_LOWERCASE / LCMAP_UPPERCASE, one code unit at a time. Not a
 * transcription of a Unicode table -- the export's own mapping.
 *
 * Supplementary characters are NOT in these tables: LCMapStringW maps a surrogate PAIR for exactly one
 * block, Deseret U+10400..U+10427 -> +0x28 (discovery/pathmakeprettyw_contract.c enumerated all 1M
 * supplementary code points), and impl.asm handles that pair rule itself. wia_pmp_init() refuses to
 * return success unless that is still the whole story, and unless the map below 0x80 is the ASCII
 * range rule the vector path relies on.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

unsigned short wia_pmp_dn[65536], wia_pmp_up[65536];

int wia_pmp_init(void) {
    for (unsigned c = 0; c < 65536; ++c) {
        wchar_t s = (wchar_t)c, d = 0, u = 0;
        if (LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE, &s, 1, &d, 1) != 1) d = s;
        if (LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_UPPERCASE, &s, 1, &u, 1) != 1) u = s;
        wia_pmp_dn[c] = d; wia_pmp_up[c] = u;
    }
    if (wia_pmp_dn[0] != 0 || wia_pmp_up[0] != 0) return 0;
    for (unsigned c = 1; c < 0x80; ++c)
        if (wia_pmp_dn[c] != ((c >= 'A' && c <= 'Z') ? c + 32 : c)) return 0;
    for (unsigned c = 0xD800; c < 0xE000; ++c)
        if (wia_pmp_dn[c] != c || wia_pmp_up[c] != c) return 0;
    for (unsigned cp = 0x10000; cp <= 0x10FFFF; ++cp) {
        unsigned v = cp - 0x10000;
        wchar_t s[2] = { (wchar_t)(0xD800 + (v >> 10)), (wchar_t)(0xDC00 + (v & 0x3FF)) }, d[2] = { 0 };
        LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE, s, 2, d, 2);
        unsigned expect = (cp >= 0x10400 && cp <= 0x10427) ? cp + 0x28 : cp;
        unsigned w = 0x10000 + (expect - 0x10000);
        wchar_t e0 = (wchar_t)(0xD800 + ((w - 0x10000) >> 10)), e1 = (wchar_t)(0xDC00 + ((w - 0x10000) & 0x3FF));
        if (d[0] != e0 || d[1] != e1) return 0;
    }
    return 1;
}
