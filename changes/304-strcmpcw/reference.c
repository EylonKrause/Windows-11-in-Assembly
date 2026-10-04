/* changes/304-strcmpcw/reference.c
 * Scalar oracle for shlwapi!StrCmpCW / StrCmpICW / StrCmpNCW / StrCmpNICW, as
 * discovery/strcmpc_contract.c measured them: ordinal on unsigned 16-bit units, the value is the
 * difference of the first unequal (folded) pair, the I forms fold 'A'..'Z' and nothing else, the N
 * forms count down an unsigned 32-bit n and never read index n.
 */
#include <wchar.h>

static int fold(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int ref_cmpw(const wchar_t* a, const wchar_t* b, int ignoreCase, int bounded, int n) {
    unsigned cnt = (unsigned)n;
    if (bounded && cnt == 0) return 0;
    for (;;) {
        int x = a[0], y = b[0];
        if (ignoreCase) { x = fold(x); y = fold(y); }
        if (x != y || x == 0) return x - y;
        if (bounded && --cnt == 0) return 0;
        ++a; ++b;
    }
}
