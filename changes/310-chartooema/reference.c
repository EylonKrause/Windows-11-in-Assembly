/* changes/310-chartooema/reference.c
 * Scalar oracle for user32's ANSI <-> OEM converters: the exports' own loops, transcribed from their
 * disassembly -- including the string form's second read of the source byte after the store, which
 * is what decides where an overlapping conversion stops. The tables are the exports' (the gate
 * compares every byte value against the live functions separately).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern unsigned char wia_c2oa_map[256], wia_o2ca_map[256];

static BOOL buffa(const unsigned char* t, LPCSTR s, LPSTR d, DWORD n) {
    if (!s || !d) return FALSE;
    if (n == 0) return TRUE;
    DWORD i = 0;
    do { d[i] = (char)t[(unsigned char)s[i]]; ++i; } while (--n);
    return TRUE;
}
static BOOL stra(const unsigned char* t, LPCSTR s, LPSTR d) {
    if (!s || !d) return FALSE;
    size_t i = 0;
    for (;;) {
        d[i] = (char)t[(unsigned char)s[i]];
        if (s[i] == 0) break;                   /* read again, after the store */
        ++i;
    }
    return TRUE;
}
BOOL ref_c2ob(LPCSTR s, LPSTR d, DWORD n) { return buffa(wia_c2oa_map, s, d, n); }
BOOL ref_c2o(LPCSTR s, LPSTR d)           { return stra(wia_c2oa_map, s, d); }
BOOL ref_o2cb(LPCSTR s, LPSTR d, DWORD n) { return buffa(wia_o2ca_map, s, d, n); }
BOOL ref_o2c(LPCSTR s, LPSTR d)           { return stra(wia_o2ca_map, s, d); }
