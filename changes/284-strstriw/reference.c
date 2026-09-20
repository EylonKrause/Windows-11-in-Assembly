/* changes/284-strstriw/reference.c
 *
 * The scalar model for shlwapi!StrStrIW, written from the contract measured in probes/contract.c:
 *
 *   * the comparison is PER CHARACTER over change 281's relation, not a collation over spans;
 *   * the FIRST match is returned;
 *   * a match may START only at a real character -- the highest start is hlen-1, never hlen;
 *   * past the terminator the haystack behaves as an endless run of NULs, and those NULs are never
 *     loaded: with non-zero memory after the terminator, {Q,SHY,SHY} is found and {Q,W} is not;
 *   * so a needle LONGER than the whole string can match;
 *   * An empty needle is a one-character needle whose character is the terminator: the answer is the
 *     FIRST code unit matching a NUL, or NULL if there is none. This is NOT what StrRStrIW does --
 *     that one refuses an empty needle outright, whatever the haystack holds (probes/emptyneedle.c).
 *     An empty string, and any NULL argument, give NULL.
 *
 * It is deliberately written the DIRECT way: ask for the effective character at an index, which is a
 * NUL past the end. impl.asm instead bounds the candidate range by the needle's NUL-matching suffix
 * and then never looks past the end at all. Two different routes to the same rule, sharing only the
 * match predicate -- so a bug in the bound shows up as a disagreement rather than being reproduced on
 * both sides. Change 283 is why that matters: there, both sides once encoded the same wrong
 * assumption and only the live export disagreed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int wia_sci_match(unsigned needle, unsigned w);

const wchar_t* ref_strstriw(const wchar_t* hay, const wchar_t* needle)
{
    size_t hlen = 0, nlen = 0, q, k;

    if (!hay || !needle) return 0;
    while (needle[nlen]) ++nlen;
    if (!nlen) nlen = 1;                    /* the empty needle IS the one-character needle {0} */
    while (hay[hlen]) ++hlen;
    if (!hlen) return 0;                    /* an empty string has no candidate position */

    for (q = 0; q < hlen; ++q) {            /* a match may start only at a real character */
        for (k = 0; k < nlen; ++k) {
            size_t idx = q + k;
            unsigned short h = (idx < hlen) ? (unsigned short)hay[idx] : 0u;
            if (!wia_sci_match((unsigned short)needle[k], h)) break;
        }
        if (k == nlen) return hay + q;       /* the FIRST such q */
    }
    return 0;
}
