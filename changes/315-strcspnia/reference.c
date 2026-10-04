/* changes/315-strcspnia/reference.c
 * Scalar oracle for StrCSpnIA on a single-byte ANSI code page, by the export's own loop and the
 * documented comparison: for each character of s, every character of the set is compared with it by
 * CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE | LOCALE_USE_CP_ACP) on one-character strings.
 * No tables: this checks tables.c's extraction rather than sharing it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int match(char b, char n) {
    char x[2] = { b, 0 }, y[2] = { n, 0 };
    return CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE | LOCALE_USE_CP_ACP, x, -1, y, -1) == CSTR_EQUAL;
}

int ref_strcspnia(const char* s, const char* set) {
    if (!s || !set) return 0;
    const char* p = s;
    for (; *p; ++p)
        for (const char* q = set; *q; ++q)
            if (match(*q, *p)) return (int)(p - s);
    return (int)(p - s);
}
