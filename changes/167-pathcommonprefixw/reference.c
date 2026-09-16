/* changes/167-pathcommonprefixw/reference.c
 *
 * An INDEPENDENT oracle for PathCommonPrefixW.
 *
 * IT IS DELIBERATELY A DIFFERENT FORMULATION FROM impl.asm, not a second copy of one. The shipped
 * function walks COMPONENT BY COMPONENT: scan each side to the next '\' or NUL, require the two
 * components to be the same LENGTH, compare them case-insensitively, record the boundary at the end
 * of each component that matched. This file is that, transcribed literally.
 *
 * impl.asm instead does a single LOCKSTEP WALK and then decides the boundary from where it stopped,
 * because that is what vectorises. The two are equivalent, and the argument is short but not
 * obvious:
 *
 *   * everything before the stop matched, and '\' upcases to itself, so p1[j] == '\' exactly when
 *     p2[j] == '\' for every j before the stop -- which is why one backward scan of p1 finds a
 *     boundary that is valid for both;
 *   * the two component terminators need NOT be the same terminator. A NUL in one against a '\' in
 *     the other ends both components at the same length, so that component MATCHES. That single
 *     case is what makes "\a" vs "\a\" return 3.
 *
 * Running the two formulations against each other, and both against the live export, is what turns
 * that argument into evidence. If the equivalence were wrong, this is where it would show.
 *
 * THE RULES THEMSELVES were read out of kernelbase!PathCommonPrefixW (RVA 0x0CBD10) and then
 * confirmed by probes/pcp6.c over 183111 cases with zero mismatches:
 *
 *   * NULL pszFile1 or NULL pszFile2 -> 0, and achPath is NOT written;
 *   * otherwise achPath, if given, is cleared to L"" before anything else is decided;
 *   * THE ONLY ROOT HANDLING IS A DOUBLED LEADING BACKSLASH. If either path starts with "\\" then
 *     BOTH must, or the answer is 0; and each such path's cursor skips its own two characters.
 *     There is no root parser, and PathSkipRootW is never called -- which is what this change was
 *     parked on the assumption of.
 *   * the case-fold is exactly RtlUpcaseUnicodeChar (0 differences over 65534 code-unit pairs,
 *     against 947 for a plain ASCII fold);
 *   * '/' is NOT a separator;
 *   * the length is measured from pszFile1 ITSELF, so a UNC result includes the leading "\\";
 *   * A COMPUTED LENGTH OF EXACTLY 2 IS REPORTED AS 3. Any length, not just drive letters, not just
 *     identical strings;
 *   * achPath receives that many characters of pszFile1, stopping at pszFile1's own terminator --
 *     which is why a result of 3 can still write only 2 -- and nothing at all if the result is 260
 *     or more.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

extern unsigned short wia_upcase[65536];

static int ref_is_unc(const wchar_t* p)
{
    return p[0] == L'\\' && p[1] == L'\\';
}

int ref_pathcommonprefixw(const wchar_t* f1, const wchar_t* f2, wchar_t* out)
{
    const wchar_t *p1, *p2, *boundary = 0;
    int ret = 0;

    if (!f1 || !f2) return 0;
    if (out) *out = 0;

    p1 = f1;
    p2 = f2;
    if (f1[0] == L'\\' && f1[1] == L'\\') {
        if (!ref_is_unc(f2)) return 0;
        p1 = f1 + 2;
    }
    if (f2[0] == L'\\' && f2[1] == L'\\') {
        if (!ref_is_unc(f1)) return 0;
        p2 = f2 + 2;
    }

    /* the component loop, exactly as the shipped one is written */
    for (;;) {
        const wchar_t *e1 = p1, *e2 = p2;
        size_t l1, l2, k;
        int equal = 1;

        while (*e1 && *e1 != L'\\') ++e1;
        while (*e2 && *e2 != L'\\') ++e2;
        l1 = (size_t)(e1 - p1);
        l2 = (size_t)(e2 - p2);
        if (l1 != l2) break;
        for (k = 0; k < l1; ++k)
            if (wia_upcase[(unsigned short)p1[k]] != wia_upcase[(unsigned short)p2[k]]) {
                equal = 0;
                break;
            }
        if (!equal) break;

        boundary = e1;                     /* this component matched */
        if (*e1 == 0) break;
        p1 = e1 + 1;
        if (*e2 == 0) break;
        p2 = e2 + 1;
    }

    if (boundary) {
        int n = (int)(boundary - f1);
        ret = (n == 2) ? 3 : n;
    }
    if (out && ret < 260) {
        int k = 0;
        while (k < ret && f1[k]) { out[k] = f1[k]; ++k; }
        out[k] = 0;
    }
    return ret;
}
