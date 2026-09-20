/* changes/283-strrstriw/reference.c
 *
 * The scalar model for shlwapi!StrRStrIW, written from the measured contract:
 *
 *   * the comparison is PER CHARACTER over change 281's relation, not a collation over spans --
 *     probes/contract.c: "ab<SOFT HYPHEN>cd" does not contain "abc";
 *   * `end` bounds only where a match may START, exclusively;
 *   * a match may START only at a real character -- the highest candidate is the LAST character of
 *     the string, hlen-1, NOT hlen-nlen. This model said hlen-nlen in its first version and was
 *     wrong, together with impl.asm, and the two agreeing with each other is precisely why the gate
 *     is three-way against the LIVE export as well: probes/pastnul.c measured the export matching
 *     {Q, soft hyphen} at the last character of "zzzq", because the soft hyphen is one of the 3238
 *     code units that match a NUL (change 282) and the comparison runs straight through the
 *     terminator. So a needle LONGER than the whole string can match too;
 *   * an empty needle, an empty string, and any NULL argument give NULL.
 *
 * It exists so the gate is THREE-WAY, and it shares only the match predicate with impl.asm: no
 * first-character filter, no backward block scan, no masks. A bug in the filter -- a candidate
 * skipped because its first character was mis-tested -- shows up as a disagreement instead of
 * being reproduced on both sides.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int wia_sci_match(unsigned needle, unsigned w);

const wchar_t* ref_strrstriw(const wchar_t* start, const wchar_t* end, const wchar_t* needle)
{
    size_t hlen = 0, nlen = 0, cand, k;
    if (!start || !end || !needle) return 0;
    if (end <= start) return 0;
    while (needle[nlen]) ++nlen;
    if (!nlen) return 0;
    while (start[hlen]) ++hlen;
    if (!hlen) return 0;                  /* an empty string has no candidate position */

    /* The haystack behaves as though the terminator were followed by endless NULs: the export
       compares the needle's remaining characters against a NUL rather than against the memory that
       is really there, and never loads past the terminator at all (probes/pastnul2.c). This model is
       written the direct way -- ask for the effective character, which is a NUL past the end -- so
       that it shares no structure with impl.asm, which instead bounds the candidate range by the
       needle's NUL-matching suffix and then never looks past the end. Two different routes to the
       same rule; if either is wrong the gate disagrees. */
    {
        long long q = (long long)hlen - 1;
        long long lim = (long long)(end - start) - 1;
        if (q > lim) q = lim;
        for (; q >= 0; --q) {
            for (k = 0; k < nlen; ++k) {
                size_t idx = (size_t)q + k;
                unsigned short h = (idx < hlen) ? (unsigned short)start[idx] : 0u;
                if (!wia_sci_match((unsigned short)needle[k], h)) break;
            }
            if (k == nlen) return start + (size_t)q;
        }
    }
    (void)cand;
    return 0;
}
