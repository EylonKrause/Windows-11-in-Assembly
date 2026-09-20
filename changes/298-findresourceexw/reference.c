/* changes/298-findresourceexw/reference.c
 *
 * The ORACLE for kernel32/kernelbase!FindResourceExW. Deliberately naive and
 * obviously correct. Every rule below was PROVED against the live export by
 * probes/contract.c and read out of the shipped disassembly (probes/*.txt in
 * RESULTS.md), not taken from MSDN.
 *
 * WHAT THE SHIPPED FUNCTION IS
 * ----------------------------
 * kernel32!FindResourceExW is one `jmp qword ptr [IAT]` into kernelbase. The
 * kernelbase body is:
 *
 *   1. norm(lpType)  -> ULONG_PTR, or (ULONG_PTR)-1 on a malformed "#" form
 *   2. norm(lpName)  -> same, and NOT evaluated at all if step 1 failed
 *   3. if (hModule == NULL) hModule = PEB->ImageBaseAddress
 *   4. ids[] = { type, name, (ULONG_PTR)(USHORT)wLanguage };
 *      status = ntdll!LdrFindResource_U(hModule, ids, 3, &entry);
 *   5. free whatever norm() allocated
 *   6. status < 0 ? (BaseSetLastNTError(status), NULL) : entry
 *
 * and norm(x) is:
 *
 *   x < 0x10000                -> x unchanged (the MAKEINTRESOURCE integer form)
 *   x[0] == L'#'               -> RtlUnicodeStringToInteger(x + 1, base 10, &v);
 *                                 if that fails, or v & 0xFFFF0000, the whole call
 *                                 fails with ERROR_INVALID_PARAMETER
 *   otherwise                  -> an UPCASED copy of the NUL-terminated string,
 *                                 one RtlUpcaseUnicodeChar CALL per character,
 *                                 into a heap block of (wcslen(x)+1)*2 bytes
 *
 * PROVED CONTRACT POINTS (probes/contract.c output is quoted in RESULTS.md)
 * ------------------------------------------------------------------------
 *  * RtlUpcaseUnicodeChar over all 65536 code units: for every c < 0x80 it is
 *    EXACTLY the a-z fold, 26 code units change and there is not one exception.
 *    973 code units change in total; the first one at or above 0x80 is U+00E0.
 *    So an ASCII-only vector fold is bit-exact, and anything >= 0x80 must go
 *    through the real table.
 *  * "#65535" -> looked up as ID 65535 (miss, 1814).  "#65536" -> 87.
 *    "#4294967296" -> ID 0 (1814): the parse wraps mod 2^32 and is NOT an error.
 *    "#" -> 87.  "#abc" -> ID 0 (1814).  "# 45" and "#+45" -> ID 45 (found).
 *    "#-45" -> 87.  "#45x" and "#45 " -> ID 45 (found).  "#0x2d" -> ID 0.
 *    All of that is simply whatever RtlUnicodeStringToInteger does, so the
 *    reference calls it too rather than guessing at it.
 *  * A successful call PRESERVES the caller's last error (checked: 12345 in,
 *    12345 out). Only the failure path touches it.
 *  * hModule == NULL means this process's own image, not kernel32.
 *  * An invalid hModule (0x30000) does NOT fault: it returns NULL / 1812.
 *  * The type is looked up before the name, so a missing name in a module whose
 *    type table is large still reports 1813 in some cases -- whatever ntdll
 *    returns is forwarded verbatim, so the reference does not model it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdlib.h>
#include <intrin.h>

#define WIA_STATUS_INVALID_PARAMETER ((LONG)0xC000000DL)

/* ---- the ntdll pieces the shipped code uses, imported the same way it does ---- */
NTSYSAPI LONG    NTAPI LdrFindResource_U(PVOID, const ULONG_PTR*, ULONG, void**);
NTSYSAPI WCHAR   NTAPI RtlUpcaseUnicodeChar(WCHAR);
NTSYSAPI LONG    NTAPI RtlUnicodeStringToInteger(PCUNICODE_STRING, ULONG, PULONG);
/* RtlNtStatusToDosError is already declared by <winternl.h> */

/* The naive upcase copy. dst must have room for wcslen(src)+1 WCHARs.
   Exported so correctness.c can compare the assembly normaliser directly, over a
   far harder corpus than resource lookups alone can reach. Returns dst. */
wchar_t* ref_resname_upcase(wchar_t* dst, const wchar_t* src)
{
    size_t i = 0;
    while (src[i] != 0) {
        dst[i] = RtlUpcaseUnicodeChar(src[i]);
        ++i;
    }
    dst[i] = 0;
    return dst;
}

/* norm(). Returns (ULONG_PTR)-1 on the malformed "#" form. On the string form it
   stores the allocation in *owned so the caller can free it. */
static ULONG_PTR ref_norm(const wchar_t* s, wchar_t** owned)
{
    size_t len, i;
    wchar_t* copy;

    *owned = NULL;
    if ((ULONG_PTR)s < 0x10000) return (ULONG_PTR)s;      /* MAKEINTRESOURCE */

    if (s[0] == L'#') {                                    /* the decimal form */
        UNICODE_STRING us;
        ULONG v = 0;
        LONG st;
        size_t n = 0;
        while (s[1 + n] != 0) ++n;
        us.Buffer = (PWSTR)(s + 1);
        us.Length = (USHORT)(n * 2);
        us.MaximumLength = (USHORT)(n * 2 + 2);
        st = RtlUnicodeStringToInteger(&us, 10, &v);
        if (st < 0) return (ULONG_PTR)-1;
        if (v & 0xFFFF0000u) return (ULONG_PTR)-1;
        return (ULONG_PTR)v;
    }

    len = 0;
    while (s[len] != 0) ++len;
    copy = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (copy == NULL) return (ULONG_PTR)-1;
    for (i = 0; i < len; ++i) copy[i] = RtlUpcaseUnicodeChar(s[i]);
    copy[len] = 0;
    *owned = copy;
    return (ULONG_PTR)copy;
}

HRSRC ref_findresourceexw(HMODULE hModule, const wchar_t* lpType,
                          const wchar_t* lpName, WORD wLanguage)
{
    ULONG_PTR ids[3];
    wchar_t* owned_type = NULL;
    wchar_t* owned_name = NULL;
    void* entry = NULL;
    LONG status = 0;

    ids[0] = ref_norm(lpType, &owned_type);
    if (ids[0] == (ULONG_PTR)-1) {
        status = WIA_STATUS_INVALID_PARAMETER;             /* name is NOT normalised */
    } else {
        ids[1] = ref_norm(lpName, &owned_name);
        if (ids[1] == (ULONG_PTR)-1) {
            status = WIA_STATUS_INVALID_PARAMETER;
        } else {
            ids[2] = (ULONG_PTR)(USHORT)wLanguage;
            if (hModule == NULL)
                hModule = (HMODULE)(*(void**)((char*)__readgsqword(0x60) + 0x10));
            status = LdrFindResource_U((PVOID)hModule, ids, 3, &entry);
        }
    }

    if (owned_type) free(owned_type);
    if (owned_name) free(owned_name);

    if (status < 0) {
        SetLastError(RtlNtStatusToDosError(status));
        return NULL;
    }
    return (HRSRC)entry;
}
