/* changes/254-findstringordinal/reference.c
 *
 * The independent oracle for FindStringOrdinal.
 *
 * It shares nothing with impl.asm but the contract. impl.asm filters sixteen positions at a time
 * with two chosen vector anchors, folds through a 65536-entry case-partner table built once, and
 * runs FIND_FROMEND as a genuinely backward block walk. This does the simplest thing that could
 * possibly be right: a plain nested loop, one character at a time, folding by CALLING
 * ntdll!RtlUpcaseUnicodeChar on every character, and FIND_FROMEND as a forward scan that keeps the
 * last hit.
 *
 * The three independences that matter:
 *   * THE SEARCH is unfiltered and unblocked, so a wrong candidate filter cannot be mirrored here;
 *   * THE DIRECTION is implemented the other way round, so an off-by-one in the backward block walk
 *     cannot be mirrored either, "keep the last hit" is slow and obviously correct, which is
 *     exactly what an oracle should be;
 *   * The fold calls the live OS function per character rather than reading our table, so a wrong
 *     or UNINITIALISED table cannot be mirrored. That is not hypothetical: change 065 was accused
 *     of a formatting bug for an entire session because its table had never been built, and the
 *     failure was silent, correct status, correct size, truncated output.
 *
 * It also reproduces the REFUSAL contract, in the order probes/errors.c measured it, because the
 * last-error value is as observable as the return value and is part of what must match.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define RF_STARTSWITH 0x00100000
#define RF_ENDSWITH   0x00200000
#define RF_FROMSTART  0x00400000
#define RF_FROMEND    0x00800000
#define RF_ALL        0x00F00000

typedef WCHAR (NTAPI *REF_UPC)(WCHAR);
static REF_UPC ref_upc;

void ref_init(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    if (!h) h = LoadLibraryW(L"ntdll.dll");
    ref_upc = (REF_UPC)GetProcAddress(h, "RtlUpcaseUnicodeChar");
}

static int ref_eq(const wchar_t* a, const wchar_t* b, int m, int ci)
{
    int j;
    for (j = 0; j < m; ++j) {
        if (a[j] == b[j]) continue;
        if (!ci) return 0;
        if (ref_upc(a[j]) != ref_upc(b[j])) return 0;
    }
    return 1;
}

int ref_findstringordinal(DWORD flags, const wchar_t* src, int cchSrc,
                          const wchar_t* val, int cchVal, BOOL ignoreCase)
{
    int n, m, i, last;
    DWORD eff;

    SetLastError(0);                                  /* before anything else -- measured */

    if ((DWORD)ignoreCase > 1)              { SetLastError(ERROR_INVALID_PARAMETER); return -1; }
    if (!src)                               { SetLastError(ERROR_INVALID_PARAMETER); return -1; }
    if (!val)                               { SetLastError(ERROR_INVALID_PARAMETER); return -1; }
    if (cchSrc < -1)                        { SetLastError(ERROR_INVALID_PARAMETER); return -1; }
    if (cchVal < -1)                        { SetLastError(ERROR_INVALID_PARAMETER); return -1; }

    eff = (flags & RF_ALL) ? flags : (flags | RF_FROMSTART);
    if ((eff & RF_ALL) & ((eff & RF_ALL) - 1))  { SetLastError(ERROR_INVALID_FLAGS); return -1; }
    if (eff & ~(DWORD)RF_ALL)                   { SetLastError(ERROR_INVALID_FLAGS); return -1; }

    n = (cchSrc == -1) ? (int)wcslen(src) : cchSrc;   /* -1 is the ONLY place a NUL matters */
    m = (cchVal == -1) ? (int)wcslen(val) : cchVal;

    if (m == 0) return (eff & (RF_FROMEND | RF_ENDSWITH)) ? n : 0;
    if (m > n) return -1;                             /* an ordinary miss: last error untouched */

    if (eff & RF_STARTSWITH) return ref_eq(src, val, m, ignoreCase) ? 0 : -1;
    if (eff & RF_ENDSWITH)   return ref_eq(src + (n - m), val, m, ignoreCase) ? (n - m) : -1;

    if (eff & RF_FROMEND) {
        last = -1;
        for (i = 0; i + m <= n; ++i)                  /* forwards, keeping the LAST hit: slow and
                                                        obviously right, which is the point */
            if (ref_eq(src + i, val, m, ignoreCase)) last = i;
        return last;
    }
    for (i = 0; i + m <= n; ++i)
        if (ref_eq(src + i, val, m, ignoreCase)) return i;
    return -1;
}
