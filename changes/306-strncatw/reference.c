/* changes/306-strncatw/reference.c
 * Scalar oracle for shlwapi!StrNCatW, transcribed from its disassembly and checked by
 * discovery/strncatw_contract.c: NULL either side returns dst untouched; the end of dst is found before
 * anything is written; n <= 0 copies nothing (n < 0 re-writes the NUL at the end); otherwise a forward
 * character loop reads src[0..n-1] until a NUL, and if none comes the n-th slot becomes the NUL.
 */
#include <wchar.h>

wchar_t* ref_strncatw(wchar_t* d, const wchar_t* s, int n) {
    if (!d || !s) return d;
    wchar_t* p = d;
    while (*p) ++p;
    if (n <= 0) { if (n) *p = 0; return d; }
    for (int i = 0; ; ++i) {
        wchar_t c = s[i];
        if (c == 0 || i == n - 1) { p[i] = 0; return d; }
        p[i] = c;
    }
}
