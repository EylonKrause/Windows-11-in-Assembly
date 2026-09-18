/* changes/285-strcspniw/reference.c
 *
 * THE SCALAR MODEL for shlwapi!StrCSpnIW, written from the contract measured in probes/contract.c and
 * probes/relation.c:
 *
 *   * it returns a COUNT: the number of leading characters of the string that are NOT in the set,
 *     equivalently the index of the first one that IS. A full int -- 66000 characters of 'a' with a
 *     set of "z" returns 66000, so nothing here is 16-bit;
 *   * the relation is change 281's, EXACTLY. probes/relation.c extracted StrCSpnIW's own relation over
 *     786420 pairs using StrCSpnIW({c},{m}) == 0 as a membership oracle and found ZERO disagreements
 *     with change 281's tables, on twelve members chosen to include both ignorable bitmap sets, the
 *     intransitive triple, and the five-partner 'K'. It is symmetric in the export itself, and a
 *     multi-member set is exactly the UNION of its members' rows -- checked over all 65535 code units
 *     for a four-member set;
 *   * an empty set gives the length, an empty string gives 0, and any NULL argument gives 0;
 *   * an embedded NUL ends the scan.
 *
 * AND THE ONE SIMPLIFICATION THAT MAKES THIS CHANGE SMALL. Changes 283 and 284 both had to model a
 * virtual NUL run past the terminator, because 3320 code units match a NUL and a needle could match
 * across the end. Here that cannot be observed AT ALL: if the set contains a NUL-matching code unit
 * then the terminator "matches" and the answer is the length; if it does not, the scan runs out and the
 * answer is the length. Both give the same number, for every string and every set.
 *
 * So this model treats the terminator as a member of the set unconditionally -- the answer is the
 * index of the first character that is NUL or in the set -- and it is exact rather than an
 * approximation. impl.asm relies on the same fact to fold the terminator test into the vector scan,
 * which is why it never needs the string's length at all.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int wia_sci_match(unsigned needle, unsigned w);

int ref_strcspniw(const wchar_t* str, const wchar_t* set)
{
    int i;
    if (!str || !set) return 0;

    for (i = 0; ; ++i) {
        unsigned c = (unsigned short)str[i];
        const wchar_t* s;
        if (!c) return i;                   /* the terminator ends it, whatever the set holds */
        for (s = set; *s; ++s)
            if (wia_sci_match((unsigned short)*s, c)) return i;
    }
}
