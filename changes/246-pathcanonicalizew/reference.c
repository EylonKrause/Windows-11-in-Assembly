/* changes/246-pathcanonicalizew/reference.c
 *
 * An oracle for shlwapi!PathCanonicalizeW, built the way change 242's was: it calls CHANGE 243's
 * oracle -- wia_ref_pathcchcanonicalizeex, compiled alongside rather than copied -- and wraps it.
 *
 * That is legitimate here, and probes/compose.c is why. It established over 451 543 cases, comparing
 * the BOOL, the whole destination buffer AND GetLastError, that
 *
 *     PathCanonicalizeW(dst, src)  ==  wrap( PathCchCanonicalizeEx(dst, MAX_PATH, src, 0) )
 *
 * with zero disagreements, so the only thing left for an oracle to model is the wrapper. And 243's
 * oracle is independent of 243's assembly: it was derived from that function's own probes and then
 * agreed with the live export over 11 772 366 enumerated cases.
 *
 * THE WRAPPER, read out of kernelbase!PathCanonicalizeW at RVA 0xF0F0 and then measured:
 *
 *   * pszDst NULL                  -> FALSE, last error 87 (ERROR_INVALID_PARAMETER)
 *   * *pszDst = 0                  -> happens BEFORE pszSrc is validated, so (dst, NULL) returns
 *                                     FALSE having ALREADY CLEARED dst. Observable, and the one
 *                                     thing a careless wrapper gets wrong.
 *   * pszSrc NULL                  -> FALSE, last error 87
 *   * HRESULT >= 0                 -> TRUE, last error untouched
 *   * HRESULT < 0                  -> FALSE, and the error is the HRESULT's LOW WORD when it is a
 *                                     FACILITY_WIN32 HRESULT ((hr & 0x1FFF0000) == 0x70000), or the
 *                                     whole HRESULT otherwise
 */
#include <windows.h>

extern long wia_ref_pathcchcanonicalizeex(wchar_t* out, size_t cch, const wchar_t* in,
                                         unsigned long flags);

#define REF_MAX_PATH_CCH 0x104u                   /* the cch the shipped envelope passes */

int wia_ref_pathcanonicalizew(wchar_t* pszDst, const wchar_t* pszSrc)
{
    long hr;
    if (pszDst == 0) { SetLastError(87); return 0; }
    pszDst[0] = 0;                                /* before pszSrc is looked at -- measured */
    if (pszSrc == 0) { SetLastError(87); return 0; }
    hr = wia_ref_pathcchcanonicalizeex(pszDst, REF_MAX_PATH_CCH, pszSrc, 0);
    if (hr >= 0) return 1;
    SetLastError(((hr & 0x1FFF0000) == 0x70000) ? (unsigned long)(unsigned short)hr
                                                : (unsigned long)hr);
    return 0;
}
