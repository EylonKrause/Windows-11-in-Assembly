/* changes/252-rtlfindunicodesubstring/reference.c
 *
 * The independent oracle for RtlFindUnicodeSubstring.
 *
 * It is deliberately written to share nothing with impl.asm except the contract. impl.asm filters
 * sixteen positions at a time with two vector anchors, folds with a 65536-entry table built once,
 * and approximates case-insensitivity in the filter before settling it exactly. This does the
 * simplest thing that could possibly be right:
 *
 *     for every start position, compare every character, one at a time,
 *     and fold by calling ntdll!RtlUpcaseUnicodeChar on every character.
 *
 * The two independences that matter:
 *   * THE SEARCH is a plain nested loop with no filter, no blocking and no early exit beyond the
 *     first mismatch, so a wrong candidate-filter in impl.asm cannot be mirrored here;
 *   * The fold calls the live OS function per character rather than reading our table, so a wrong
 *     or uninitialised table in impl.asm cannot be mirrored here either. That second point is not
 *     hypothetical: change 065 was accused of a formatting bug for an entire session because its
 *     two-digit table had never been built, and the failure was silent -- correct status, correct
 *     size, truncated output. An oracle that reads the same table would have agreed with the bug.
 *
 * It is O(n*m) with a function call per character, so it is far slower than the shipped code. That
 * is fine: it is never benchmarked, only believed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } REF_USTR;
typedef WCHAR (NTAPI *REF_UPC)(WCHAR);

static REF_UPC ref_upc;

void ref_init(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    if (!h) h = LoadLibraryW(L"ntdll.dll");
    ref_upc = (REF_UPC)GetProcAddress(h, "RtlUpcaseUnicodeChar");
}

PWSTR ref_findunicodesubstring(void* FullV, void* SearchV, BOOLEAN CaseInSensitive)
{
    REF_USTR* Full   = (REF_USTR*)FullV;
    REF_USTR* Search = (REF_USTR*)SearchV;
    int n, m, i, j;

    if (!Full || !Search) return 0;

    n = (int)(Full->Length   / 2);            /* these are COUNTED strings: Length is in bytes */
    m = (int)(Search->Length / 2);

    if (m == 0) return Full->Buffer;          /* an empty needle matches at offset 0, always */
    if (m > n)  return 0;

    for (i = 0; i + m <= n; ++i) {
        for (j = 0; j < m; ++j) {
            WCHAR a = Full->Buffer[i + j];
            WCHAR b = Search->Buffer[j];
            if (a == b) continue;
            if (!CaseInSensitive) break;
            if (ref_upc(a) != ref_upc(b)) break;
        }
        if (j == m) return Full->Buffer + i;  /* the FIRST match wins */
    }
    return 0;
}
