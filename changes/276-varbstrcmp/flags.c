/* changes/276-varbstrcmp/flags.c
 *
 * THE ACCEPTED FLAG BITS, DERIVED FROM THE OS, AND THE VALIDATION THE FAST PATH MUST NOT SKIP.
 *
 * probes/errors.c established the rule that shapes this whole change:
 *
 *     ""    vs "",  flags 0x40   -> VARCMP_EQ        the EMPTY rules do not validate
 *     ""    vs "",  bad lcid     -> VARCMP_EQ
 *     "abc" vs "",  flags 0x40   -> VARCMP_GT
 *     "abc" vs "abc", flags 0x40 -> E_INVALIDARG     a NON-EMPTY pair always does
 *     x     vs x,     flags 0x40 -> E_INVALIDARG     ... even the SAME POINTER
 *
 * So an implementation may answer the empty cases from the lengths alone, and may NOT answer
 * "these are identical, therefore equal" without first checking that the locale and flags are ones
 * the OS accepts. A fast path that skipped that would turn E_INVALIDARG into VARCMP_EQ on every bad
 * argument, which is a wrong answer that looks like a right one.
 *
 * THE MASK IS DERIVED, NOT LISTED. probes/errors.c asked CompareStringW bit by bit and got twelve
 * accepted bits, mask 0x5803103F on this machine -- and VarBstrCmp agreed with it on every one of
 * the thirty-two. Writing 0x5803103F into the source would be a constant nobody can check by reading
 * it and one that a future Windows could move; asking the OS costs thirty-two calls, once. It is the
 * same decision change 269 made for its alias table and change 210 for its upcase table.
 *
 * THE LOCALE CANNOT BE MASKED, so it is validated by asking CompareStringW to compare one character
 * -- which probes/gap.c measured at 25.75 ns. That is why the fast path has a LENGTH THRESHOLD: at
 * sixteen characters the collation the OS would otherwise do already costs more than the validation,
 * and below it the implementation simply delegates and is a lean wrapper instead.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>

unsigned long wia_vbc_mask;          /* the flag bits CompareStringW accepts on this machine */

int wia_vbc_init(void)
{
    int bit;
    wia_vbc_mask = 0;
    for (bit = 0; bit < 32; ++bit) {
        unsigned long fl = 1ul << bit;
        if (CompareStringW(LOCALE_USER_DEFAULT, fl, L"a", 1, L"b", 1) != 0)
            wia_vbc_mask |= fl;
    }
    /* A mask that came back empty would send every call down the slow path -- correct, but silently
       three times slower, which is the kind of thing that ships. */
    return wia_vbc_mask ? 0 : 1;
}

/* Returns 0 when the arguments are ones the OS accepts, 1 otherwise. Only the fast path calls it;
   the slow path learns the same thing from CompareStringW's own failure. */
int wia_vbc_validate(unsigned long lcid, unsigned long flags)
{
    if (flags & ~wia_vbc_mask) return 1;
    if (CompareStringW(lcid, flags, L"a", 1, L"a", 1) == 0) return 1;
    return 0;
}

/* The slow path: what the shipped export does, and the only thing that can do it. CompareStringW
   returns 1/2/3 and VARCMP_LT/EQ/GT are 0/1/2, so the mapping is a subtraction; 0 is its failure,
   and probes/errors.c measured that VarBstrCmp reports E_INVALIDARG for both a bad locale and a bad
   flag bit. */
long wia_vbc_compare(const wchar_t* l, int nl, const wchar_t* r, int nr,
                     unsigned long lcid, unsigned long flags)
{
    int c = CompareStringW(lcid, flags, l, nl, r, nr);
    if (c == 0) return (long)0x80070057ul;      /* E_INVALIDARG */
    return (long)(c - 1);
}
