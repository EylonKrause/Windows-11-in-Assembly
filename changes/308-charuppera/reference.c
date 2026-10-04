/* changes/308-charuppera/reference.c
 * Scalar oracle for CharUpperA / CharLowerA / CharUpperBuffA / CharLowerBuffA on a single-byte ANSI
 * code page, as the kernelbase disassembly does it -- through UTF-16 and back, one byte at a time,
 * with the documented APIs rather than the tables impl.asm uses:
 *   MultiByteToWideChar(CP_ACP) -> CharUpperBuffW / CharLowerBuffW -> WideCharToMultiByte(CP_ACP).
 * Buff forms: cch bytes, every one written, return cch. String forms: strlen + 1 bytes, return p.
 * Character mode: the value's low byte, bits 8..15 kept.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

static unsigned char map1(unsigned char b, int upper) {
    if (b == 0) return 0;
    char c = (char)b; WCHAR w = 0; char out = 0;
    MultiByteToWideChar(CP_ACP, 0, &c, 1, &w, 1);
    if (upper) CharUpperBuffW(&w, 1); else CharLowerBuffW(&w, 1);
    WideCharToMultiByte(CP_ACP, 0, &w, 1, &out, 1, NULL, NULL);
    return (unsigned char)out;
}

DWORD ref_casebuffa(char* p, DWORD cch, int upper) {
    if (cch == 0) return 0;
    for (DWORD i = 0; i < cch; ++i) p[i] = (char)map1((unsigned char)p[i], upper);
    return cch;
}

char* ref_casea(char* p, int upper) {
    if (!p) return NULL;
    if (((uintptr_t)p >> 16) == 0) {
        uintptr_t v = (uintptr_t)p;
        return (char*)((v & 0xFF00) | map1((unsigned char)(v & 0xFF), upper));
    }
    DWORD n = 0; while (p[n]) ++n;
    ref_casebuffa(p, n + 1, upper);
    return p;
}
