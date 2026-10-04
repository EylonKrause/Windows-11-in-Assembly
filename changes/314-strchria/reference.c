/* changes/314-strchria/reference.c
 * Scalar oracle for StrChrIA / StrRChrIA on a single-byte ANSI code page, by the documented route and
 * the export's own loop: each character compared with the needle by CompareStringA(LOCALE_SYSTEM_DEFAULT,
 * NORM_IGNORECASE | LOCALE_USE_CP_ACP) on two one-character strings, the needle being the low byte of
 * the WORD. No tables -- this does not share tables.c's extraction, so it checks it.
 * StrRChrIA with a NUL inside the range never returns in the export; the oracle returns REF_SPINS.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define REF_SPINS ((char*)(intptr_t)-1)

static int match(char b, char n) {
    char x[2] = { b, 0 }, y[2] = { n, 0 };
    return CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE | LOCALE_USE_CP_ACP, x, -1, y, -1) == CSTR_EQUAL;
}

char* ref_strchria(const char* s, WORD w) {
    if (!s) return NULL;
    char n = (char)(w & 0xFF);
    for (; *s; ++s) if (match(*s, n)) return (char*)s;
    return NULL;
}

char* ref_strrchria(const char* s, const char* e, WORD w) {
    if (!e) e = s + lstrlenA(s);
    char n = (char)(w & 0xFF);
    const char* last = NULL;
    for (const char* p = s; p < e; ++p) {
        if (!*p) return REF_SPINS;
        if (match(*p, n)) last = p;
    }
    return (char*)last;
}
