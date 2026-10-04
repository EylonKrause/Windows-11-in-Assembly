/* changes/305-strcmpca/reference.c
 * Scalar oracle for shlwapi!StrCmpCA / StrCmpICA / StrCmpNCA / StrCmpNICA, as
 * discovery/strcmpc_contract.c measured them over every byte pair: the C forms compare UNSIGNED bytes,
 * the I forms fold 'A'..'Z' to lower case and compare SIGNED chars; the value is the difference of the
 * first unequal (folded) pair; the N forms count down an unsigned 32-bit n and never read index n.
 */
static int fold(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int ref_cmpa(const char* a, const char* b, int ignoreCase, int bounded, int n) {
    unsigned cnt = (unsigned)n;
    if (bounded && cnt == 0) return 0;
    for (;;) {
        int x, y;
        if (ignoreCase) { x = fold((signed char)a[0]); y = fold((signed char)b[0]); }
        else            { x = (unsigned char)a[0];     y = (unsigned char)b[0]; }
        if (x != y || x == 0) return x - y;
        if (bounded && --cnt == 0) return 0;
        ++a; ++b;
    }
}
