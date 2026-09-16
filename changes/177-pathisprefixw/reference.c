/* changes/177-pathisprefixw/reference.c
 *
 * An INDEPENDENT oracle for PathIsPrefixW's ENVELOPE.
 *
 * It deliberately calls the LIVE PathCommonPrefixW for the walk, exactly as the shipped function
 * does, so that ours-vs-oracle ISOLATES the two rules this change actually adds:
 *
 *   1. the NULL contract -- every NULL combination is FALSE, including an empty prefix against a
 *      NULL path, which the identity itself cannot express because wcslen(NULL) is not evaluable;
 *   2. the comparison, `common == wcslen(pszPrefix)`.
 *
 * Anything wrong in the walk shows up instead in ours-vs-live, which covers both halves at once --
 * and that is also the check that change 167's assembly still agrees with the shipped body it is
 * standing in for.
 *
 * THE IDENTITY ITSELF was established by this change's original probes (2M cases against both live
 * exports) and re-established from scratch by probes/pip2.c now that 167 has landed: 516441 cases,
 * 0 mismatches. It is what explains every odd result the direct probing found, including the
 * trailing-backslash case -- "C:\a\" against "C:\a\b" gives a common prefix of 4 against a prefix
 * length of 5, so FALSE.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

typedef int (WINAPI *REF_FPCP)(const wchar_t*, const wchar_t*, wchar_t*);
static REF_FPCP ref_pcp;

int ref_init(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    ref_pcp = (REF_FPCP)GetProcAddress(h, "PathCommonPrefixW");
    return ref_pcp != 0;
}

int ref_pathisprefixw(const wchar_t* pszPrefix, const wchar_t* pszPath)
{
    if (!pszPrefix || !pszPath) return 0;      /* measured: FALSE for every NULL combination */
    if (!ref_pcp) return -1;                   /* ref_init was not called: fail loudly */
    return ref_pcp(pszPath, pszPrefix, 0) == (int)wcslen(pszPrefix);
}
