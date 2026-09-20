// changes/297-windowscomparestringordinal/reference.c
// The correctness oracle for combase!WindowsCompareStringOrdinal. Not fast; just obviously right.
//
// Every line of this was measured, not read off msdn. probes/wcso.c is the measurement; the
// disassembly of the shipped export in RESULTS.md is the cross-check. What it found:
//
//   * HSTRING LAYOUT. The shipped export does NOT call its own accessors; it reads the handle
//     directly: `mov r9d,[rdx+4]` is the length and `mov r8,[rdx+10h]` is the buffer. The same two
//     offsets are the whole body of WindowsGetStringLen and WindowsGetStringRawBuffer. Confirmed
//     against those two accessors over 164 live handles of every kind combase can make: heap
//     (WindowsCreateString), fast-pass (WindowsCreateStringReference), promoted
//     (WindowsPreallocateStringBuffer + WindowsPromoteStringBuffer) and WindowsSubstring.
//
//   * NULL is the empty string, and it is the only empty string the documented creators produce:
//     WindowsCreateString(L"",0) and WindowsCreateStringReference(L"",0) both hand back a NULL
//     HSTRING. NULL vs NULL is 0, NULL vs a non-empty string is -1, the reverse is 1.
//
//   * *result IS -1 / 0 / 1, never a difference: "a" vs "z" is -1, not -25.
//
//   * The return is S_OK on every success, and E_INVALIDARG (0x80070057) when `result` is NULL --
//     and that path is not silent. It calls RoOriginateErrorW(E_INVALIDARG, 6, L"result"), which
//     leaves a live IRestrictedErrorInfo on the thread. Proved by originating the identical error
//     ourselves and comparing IRestrictedErrorInfo::GetErrorDetails field by field: same HRESULT,
//     same description, same restricted description ("result"), same capability SID. The export at
//     combase+0x69530 that the shipped code calls IS RoOriginateErrorW, ordinal 0x19F, same RVA.
//
//   * Embedded NULs are ordinary characters. "a\0b" vs "a\0c" is -1, not 0: the comparison runs to
//     the declared length and does not stop at a NUL. (HSTRING allows them; that is why
//     WindowsStringHasEmbeddedNull exists.)
//
//   * Comparing a handle with itself is an early-out, `cmp rcx,rdx / je` is the second
//     instruction of the shipped body, ahead of every other test. It is also why h vs
//     WindowsDuplicateString(h) is free: a duplicate of a heap HSTRING is the SAME handle with the
//     refcount bumped.
//
//   * It really is ordinal, which is the only reason this function is in this repository at all.
//     discovery/strchri_is_linguistic.c and discovery/strcmpn_is_linguistic.c ruled the StrCmp /
//     StrChrI family OUT because those fold through the locale machinery. Here: 400 000 random
//     pairs over an alphabet loaded with case pairs, ignorables, combining marks, sharp-s,
//     U+0130/U+0131, lone and paired surrogates, PUA and non-characters produced ZERO differences
//     against the plain code-unit compare below, on a corpus where a linguistic CompareStringW
//     disagrees with it 19.7% of the time. Identical under en-US, tr-TR, lt-LT, az-Latn-AZ, el-GR
//     and ja-JP. Ordering is by UTF-16 code unit, not code point: U+ffff > U+10000.
//
//   * The one thing that is not the empty-string model. a non-NULL handle whose buffer is NULL
//     compares EQUAL to everything ("abc" included) and leaves GetLastError() == 87. That is
//     not a rule anyone wrote down; it falls out of the implementation. The shipped body forwards
//     to kernelbase!CompareStringOrdinal, which rejects a NULL lpString with 0 /
//     ERROR_INVALID_PARAMETER, and the shipped body maps "not 1 and not 3" to *result = 0. No
//     documented creator can build such a handle, so this is only reachable through a hand-built
//     header, and the corpus builds them, because the layout is proved and the gate is exact.
//
// A successful call does NOT disturb GetLastError.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* The handle, as the shipped code reads it. Only the two starred fields are ever touched. */
typedef struct WIA_HSTRING {
    UINT32          flags;      /* +0x00  1 = fast-pass (WindowsCreateStringReference)            */
    UINT32          length;     /* +0x04  * code units, exact -- embedded NULs included           */
    UINT32          pad0;       /* +0x08                                                          */
    UINT32          pad1;       /* +0x0C                                                          */
    const wchar_t*  buffer;     /* +0x10  * not necessarily NUL-terminated                        */
} WIA_HSTRING;

/* Declared here rather than via roerrorapi.h so this file needs nothing but windows.h. */
BOOL WINAPI RoOriginateErrorW(HRESULT error, UINT cchMax, PCWSTR message);

HRESULT ref_WindowsCompareStringOrdinal(void* one, void* two, INT32* result)
{
    const WIA_HSTRING* h1 = (const WIA_HSTRING*)one;
    const WIA_HSTRING* h2 = (const WIA_HSTRING*)two;
    UINT32 n1, n2, n, i;

    if (result == 0) {
        RoOriginateErrorW(E_INVALIDARG, 6, L"result");
        return E_INVALIDARG;
    }
    if (one == two) { *result = 0; return S_OK; }              /* the same handle, NULL/NULL too */
    if (two == 0)   { *result = h1->length ? 1 : 0; return S_OK; }
    if (one == 0)   { *result = h2->length ? -1 : 0; return S_OK; }

    if (h1->buffer == 0 || h2->buffer == 0) {                  /* the forged-header quirk above  */
        SetLastError(ERROR_INVALID_PARAMETER);
        *result = 0;
        return S_OK;
    }

    n1 = h1->length;
    n2 = h2->length;
    n  = (n1 < n2) ? n1 : n2;
    for (i = 0; i < n; ++i) {
        unsigned a = (unsigned short)h1->buffer[i];
        unsigned b = (unsigned short)h2->buffer[i];
        if (a != b) { *result = (a < b) ? -1 : 1; return S_OK; }
    }
    *result = (n1 == n2) ? 0 : ((n1 < n2) ? -1 : 1);           /* the shorter string is LESS     */
    return S_OK;
}
