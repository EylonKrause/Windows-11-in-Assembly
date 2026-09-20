/* changes/236-pathcommonprefixa/reference.c
   An independent scalar oracle for shlwapi!PathCommonPrefixA.

   Deliberately written the slow, obvious way -- one character at a time, a table fold, a backward
   scan for the last separator -- so that it shares no structure with impl.asm and an error common
   to both is unlikely. Every rule here was MEASURED in probes/pcpa.c, pcpa2.c, pcpa3.c, pcpa5.c
   and validated as a whole by probes/pcpa6.c, which ran this model against the live export over
   3.65 million pairs comparing the return AND a 400-byte poison window, with 0 mismatches. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>          /* for MAX_PATH, which is a measured bound here */
#include <string.h>

/* The fold, derived by enumerating all 256 x 256 ordered byte pairs in probes/pcpa2.c. It is NOT
   a case-mapping API call and must not be replaced by one: 0x88 -> 0x5E is not a case pair, and
   0xD7, 0xDF and 0xF7 are deliberately absent. */
static unsigned char FOLD[256];
static int fold_built = 0;
static void build_fold(void)
{
    int v;
    for (v = 0; v < 256; ++v) FOLD[v] = (unsigned char)v;
    for (v = 0x61; v <= 0x7A; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    for (v = 0xE0; v <= 0xF6; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    for (v = 0xF8; v <= 0xFE; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    FOLD[0x88] = 0x5E;                      /* not a case pair -- the StrStrA conflation */
    FOLD[0x9A] = 0x8A;
    FOLD[0x9C] = 0x8C;
    FOLD[0x9E] = 0x8E;
    FOLD[0xFF] = 0x9F;
    fold_built = 1;
}

int ref_pathcommonprefixa(const char* a, const char* b, char* out)
{
    int k, n, j, i, la, wn;
    if (!fold_built) build_fold();

    /* NULL writes nothing at all -- not even a terminator. The no-common-prefix case does write
       one, so the two are distinguishable only with a poison fill. */
    if (!a || !b) return 0;

    k = 0;
    while (a[k] && b[k] && FOLD[(unsigned char)a[k]] == FOLD[(unsigned char)b[k]]) ++k;

    if (!a[k] && !b[k]) {
        n = k;                                          /* the same path: no cut */
    } else if ((!a[k] && b[k] == '\\') || (!b[k] && a[k] == '\\')) {
        /* one path ends exactly where the other begins a new component -- uncut, EXCEPT when that
           whole component is a lone separator */
        n = (k == 1 && a[0] == '\\') ? 0 : k;
    } else {
        j = -1;
        for (i = 0; i < k; ++i) if (a[i] == '\\') j = i;
        if (j < 0)                          n = 0;
        else if (j == 1 && a[0] == '\\')    n = 0;      /* the UNC prefix alone is not a prefix */
        else                                n = j;
    }

    /* The drive-root fixup, and the defect it carries: a common prefix of exactly 2 is REPORTED as
       3, but the copy below is bounded by the string, so "aa" still writes only two characters and
       the returned count exceeds the string produced. Measured, not invented. */
    if (n == 2) n = 3;

    if (out) {
        /* The MAX_PATH bound, and it is on the result rather than on the inputs: 900-character
           paths whose common prefix is 15 copy normally, while identical 260-character paths do
           not. A result of 259 writes 259 characters and a terminator -- exactly MAX_PATH bytes --
           and a result of 260 writes ONLY a bare terminator. The returned count is unaffected
           either way, so a caller that trusts it gets a number with no string behind it. */
        if (n >= MAX_PATH) {
            out[0] = 0;
        } else {
            la = 0; while (a[la]) ++la;
            wn = n < la ? n : la;
            memcpy(out, a, (size_t)wn);
            out[wn] = 0;
        }
    }
    return n;
}
