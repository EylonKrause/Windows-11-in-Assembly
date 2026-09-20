/* changes/276-varbstrcmp/reference.c
 *
 * The scalar model for oleaut32!VarBstrCmp.
 *
 * It calls CompareStringW, deliberately and always, no fast path, no threshold. What the gate
 * needs a second opinion about is not the collation, which is the OS's and which nothing outside it
 * can have; it is the WRAPPER: the empty rules, the validation, the error mapping, and above all
 * whether the fast path in impl.asm ever answers something the plain delegation would not.
 *
 * The rules it states were all measured (probes/contract.c, probes/errors.c):
 *
 *   * a NULL BSTR is the same as an empty one, both ways round;
 *   * if either side is empty the answer comes from the LENGTHS ALONE and the locale and flags are
 *     never looked at, `"" vs ""` with a bad locale is EQ, `"abc" vs ""` with a bad flag is GT;
 *   * otherwise CompareStringW decides, with the BSTR's counted length (embedded NULs and all), and
 *     its 1/2/3 becomes VARCMP_LT/EQ/GT by subtracting one;
 *   * a failure is E_INVALIDARG, for a bad locale and for an undefined flag bit alike.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>

long ref_varbstrcmp(BSTR l, BSTR r, unsigned long lcid, unsigned long flags)
{
    unsigned nl = l ? SysStringLen(l) : 0u;
    unsigned nr = r ? SysStringLen(r) : 0u;
    int c;

    if (nl == 0) return (nr == 0) ? VARCMP_EQ : VARCMP_LT;
    if (nr == 0) return VARCMP_GT;

    c = CompareStringW(lcid, flags, l, (int)nl, r, (int)nr);
    if (c == 0) return (long)0x80070057ul;      /* E_INVALIDARG */
    return (long)(c - 1);
}
