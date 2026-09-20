/* changes/288-foldstringw-digits/reference.c
 *
 * The scalar model for FoldStringW's MAP_FOLDDIGITS path, from the contract measured in
 * probes/contract.c:
 *
 *     int FoldStringW(DWORD dwMapFlags, LPCWSTR src, int cchSrc, LPWSTR dest, int cchDest)
 *
 *   * This change implements one of five flag paths, and that is a measured scope rather than a
 *     convenience. MAP_FOLDDIGITS is the only strictly 1:1 flag; MAP_FOLDCZONE, MAP_PRECOMPOSED,
 *     MAP_COMPOSITE and MAP_EXPAND_LIGATURES all turn one input unit into several -- up to eighteen for
 *     one MAP_FOLDCZONE input -- so they are not per-character tables and are separate problems. This
 *     function therefore DECLINES anything but MAP_FOLDDIGITS, with ERROR_INVALID_FLAGS, and the
 *     correctness gate asserts that declining rather than leaving it to the corpus;
 *   * cchSrc > 0 is a count of code units; cchSrc == -1 means NUL-terminated and includes the
 *     TERMINATOR, so "abc" produces 4;
 *   * cchDest == 0 is a LENGTH QUERY: the required count is returned and nothing is written;
 *   * a cchDest too small returns 0 with ERROR_INSUFFICIENT_BUFFER and writes nothing -- measured:
 *     the first destination word was still its sentinel afterwards;
 *   * cchSrc == 0 returns 0 with ERROR_INVALID_PARAMETER. probes/contract.c first reported
 *     ERROR_INSUFFICIENT_BUFFER for this, and that was a MEASUREMENT BUG rather than a fact: the probe
 *     read GetLastError without resetting it first, so it reported the 122 left behind by the preceding
 *     too-small-buffer call. The correctness gate resets the error before every call and got 87. The
 *     probe now resets too;
 *   * a FORWARD, ONE-UNIT-AT-A-TIME loop is observable when the buffers overlap. dest == src is
 *     refused, but every other overlap is accepted, and probes/overlap.c shows the export's output
 *     matching a naive forward loop exactly at every offset -- it reads units it has already
 *     overwritten rather than buffering. That is reproducible, so this model's plain forward loop is
 *     already right, and impl.asm detects overlap and drops its unroll for those inputs;
 *   * a NULL source returns 0 with ERROR_INVALID_PARAMETER;
 *   * a NULL destination is refused only when cchDest is non-zero, and the refusal order is
 *     observable. This model originally had neither right: it rejected a NULL destination after the
 *     too-small-buffer test, and it inherited that ordering from impl.asm rather than from the export,
 *     so the three-way comparison could not see the error -- both sides shared the assumption. A
 *     mutation survivor (mutant #10, the deleted NULL-destination refusal, which passed 66,410 cases)
 *     sent probes/nulldest.c to ask the export, and the measured order is:
 *
 *         1. src == NULL   2. dest == src   3. cchSrc == 0   4. cchDest != 0 && dest == NULL
 *         5. an unsupported flag (1004)     6. cchDest == 0 -> the count    7. cchDest < count (122)
 *
 *     with each step observed: dest NULL and cchDest 0 SUCCEEDS; dest NULL with cchDest 2 against
 *     three units gives 87 and not 122, so step 4 precedes step 7; an unsupported flag alone gives
 *     1004 but the same flag with a NULL destination gives 87, so step 5 follows steps 1-4; and a
 *     guarded unterminated string with cchSrc -1 and a NULL destination returns 87 instead of
 *     faulting, so the refusals precede the length scan;
 *   * dest == src is refused with ERROR_INVALID_PARAMETER, and the check is POINTER EQUALITY only:
 *     dest = src+1, src+4, src+8 and src-4 all succeed, so partial overlap is permitted by the export
 *     even though the documentation calls overlap illegal.
 *
 * The model walks the table one code unit at a time and shares nothing with impl.asm beyond the table
 * itself.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern unsigned short wia_fold_digit[65536];

#define MAP_FOLDDIGITS_ 0x0080

int ref_foldstringw_digits(DWORD flags, const wchar_t* src, int cchSrc,
                           wchar_t* dest, int cchDest)
{
    int n, i;

    /* The order is measured, not chosen -- probes/nulldest.c, and see the block comment above. The
       flag test is FIFTH: a declined flag together with a bad pointer reports the pointer error. */
    if (!src)                       { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (dest == src)                { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (cchSrc == 0)                { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (cchDest != 0 && !dest)      { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (flags != MAP_FOLDDIGITS_)   { SetLastError(ERROR_INVALID_FLAGS); return 0; }

    if (cchSrc < 0) {
        n = 0;
        while (src[n]) ++n;
        ++n;                                  /* -1 INCLUDES the terminator */
    } else {
        n = cchSrc;
    }

    if (cchDest == 0) return n;               /* the length query writes nothing */
    if (cchDest < n)  { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }

    for (i = 0; i < n; ++i)
        dest[i] = (wchar_t)wia_fold_digit[(unsigned short)src[i]];
    return n;
}
