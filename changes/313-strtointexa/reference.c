/* changes/313-strtointexa/reference.c
 * Scalar oracle for StrToInt64ExA / StrToIntExA, by the export's own route: strlen, the whole string
 * to UTF-16 with MultiByteToWideChar(CP_ACP), then StrToInt64ExW's parse -- written out here from its
 * disassembly (kernelbase RVA 0xF35A0) over the WIDE copy, not over the bytes:
 *   skip L'\t', L'\n', L' '; one L'+' or L'-'; with flags bit 0 and L"0x"/L"0X", hex digits and the
 *   sign dropped; otherwise decimal digits; both mod 2^64; TRUE iff a digit was taken. The result is
 *   stored, 0 on failure, unless the pointer is NULL; a NULL string returns FALSE and stores nothing.
 * StrToIntExA stores (int) of it, or 0 when FALSE, always.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>

static BOOL parse_w(const wchar_t* p, DWORD flags, LONGLONG* out) {
    while (*p == L'\t' || *p == L'\n' || *p == L' ') ++p;
    int neg = 0;
    if (*p == L'+' || *p == L'-') { neg = (*p == L'-'); ++p; }
    unsigned long long v = 0; int any = 0;
    if ((flags & 1) && p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) {
        p += 2; neg = 0;
        for (;; ++p) {
            int d;
            if (*p >= L'0' && *p <= L'9') d = *p - L'0';
            else if (*p >= L'a' && *p <= L'f') d = *p - L'a' + 10;
            else if (*p >= L'A' && *p <= L'F') d = *p - L'A' + 10;
            else break;
            v = v * 16 + (unsigned)d; any = 1;
        }
    } else {
        for (; *p >= L'0' && *p <= L'9'; ++p) { v = v * 10 + (unsigned)(*p - L'0'); any = 1; }
    }
    if (out) *out = (LONGLONG)(neg ? 0 - v : v);
    return any;
}

BOOL ref_strtoint64exa(const char* s, DWORD flags, LONGLONG* out) {
    if (!s) return FALSE;
    size_t len = strlen(s);
    int n = MultiByteToWideChar(CP_ACP, 0, s, (int)(len + 1), NULL, 0);
    if (n <= 0) return FALSE;
    wchar_t* w = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)n);
    if (!w) return FALSE;
    MultiByteToWideChar(CP_ACP, 0, s, (int)(len + 1), w, n);
    BOOL r = parse_w(w, flags, out);
    free(w);
    return r;
}

BOOL ref_strtointexa(const char* s, DWORD flags, int* out) {
    LONGLONG v = 0;
    BOOL r = ref_strtoint64exa(s, flags, &v);
    *out = r ? (int)v : 0;
    return r;
}
