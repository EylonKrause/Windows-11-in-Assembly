/* changes/311-pathisurlw/reference.c
 * Scalar oracle for PathIsURLW / PathIsURLA, transcribed from the disassembly's loop: walk while the
 * unit is in [+-.0-9A-Za-z]; TRUE exactly when the walk stops on ':' at index 2 or later.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static int in_set(unsigned c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}
BOOL ref_pathisurlw(LPCWSTR p) {
    if (!p) return FALSE;
    size_t i = 0;
    while (in_set(p[i])) ++i;
    return p[i] == ':' && i >= 2;
}
BOOL ref_pathisurla(LPCSTR p) {
    if (!p) return FALSE;
    size_t i = 0;
    while (in_set((unsigned char)p[i])) ++i;
    return p[i] == ':' && i >= 2;
}
