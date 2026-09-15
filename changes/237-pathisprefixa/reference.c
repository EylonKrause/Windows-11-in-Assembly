/* changes/237-pathisprefixa/reference.c
   An independent scalar oracle for shlwapi!PathIsPrefixA.

   The rule, validated in probes/pipa.c over 87 067 561 pairs and in probes/pipa2.c over a further
   29 822 521 with zero disagreements:

       PathIsPrefixA(a, b)  ==  (PathCommonPrefixA(a, b, NULL) == strlen(a))

   so this oracle recomputes change 236's count -- INCLUDING the length-two defect, which is not
   incidental here: it is what makes a two-character path not a prefix of itself, and what makes a
   three-character path ending in a separator a prefix of its own first two characters.

   Written the slow, obvious way and independently of impl.asm. The MAX_PATH copy refusal from change
   236 has no role: there is no output buffer, and the COUNT was never affected by it -- confirmed
   over lengths 250..600 in probes/pipa.c. */
#include <string.h>

/* The fold, derived by enumeration in change 236's probes/pcpa2.c and RE-DERIVED for this function
   in probes/pipa.c: 376 of 64 516 ordered byte pairs are equivalent, which is the 254-value diagonal
   plus 122 folded pairs -- exactly the 61 two-member classes change 236 found, counted as ordered
   pairs. Not a case-mapping API: 0x88 -> 0x5E is not a case pair. */
static unsigned char FOLD[256];
static int fold_built = 0;
static void build_fold(void)
{
    int v;
    for (v = 0; v < 256; ++v) FOLD[v] = (unsigned char)v;
    for (v = 0x61; v <= 0x7A; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    for (v = 0xE0; v <= 0xF6; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    for (v = 0xF8; v <= 0xFE; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    FOLD[0x88] = 0x5E;
    FOLD[0x9A] = 0x8A;
    FOLD[0x9C] = 0x8C;
    FOLD[0x9E] = 0x8E;
    FOLD[0xFF] = 0x9F;
    fold_built = 1;
}

int ref_pathisprefixa(const char* a, const char* b)
{
    int k, n, j, i, la;
    if (!fold_built) build_fold();
    if (!a || !b) return 0;

    k = 0;
    while (a[k] && b[k] && FOLD[(unsigned char)a[k]] == FOLD[(unsigned char)b[k]]) ++k;

    if (!a[k] && !b[k]) {
        n = k;
    } else if ((!a[k] && b[k] == '\\') || (!b[k] && a[k] == '\\')) {
        n = (k == 1 && a[0] == '\\') ? 0 : k;
    } else {
        j = -1;
        for (i = 0; i < k; ++i) if (a[i] == '\\') j = i;
        if (j < 0)                          n = 0;
        else if (j == 1 && a[0] == '\\')    n = 0;
        else                                n = j;
    }
    if (n == 2) n = 3;              /* change 236's reported-count defect, and it MATTERS here */

    la = 0; while (a[la]) ++la;
    return n == la;
}
