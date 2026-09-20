/* changes/291-expandenvironmentstringsw/reference.c
 *
 * THE ORACLE for kernel32!ExpandEnvironmentStringsW (forwarded to kernelbase.dll, processenv).
 *
 * Deliberately naive: one character at a time, no cleverness, no vectors, no lookahead. Its only
 * job is to be obviously right. It is a TRANSCRIPTION of the two shipped functions, which were
 * disassembled first (see RESULTS.md for the listings):
 *
 *   kernelbase!ExpandEnvironmentStringsW   RVA 0xC7B10 in 10.0.26100.9278
 *       SrcLen = lpSrc ? wcslen(lpSrc) : 0
 *       st = ntdll!RtlExpandEnvironmentStrings(NULL, lpSrc, SrcLen, lpDst, nSize, &ReturnLength)
 *       if (st >= 0 || st == STATUS_BUFFER_TOO_SMALL)
 *            return ReturnLength > 0xFFFFFFFF ? (BaseSetLastNTError(STATUS_UNSUCCESSFUL), 0)
 *                                             : (DWORD)ReturnLength;
 *       BaseSetLastNTError(st); return 0;
 *
 *   ntdll!RtlExpandEnvironmentStrings       RVA 0xBB050
 *       the loop below, verbatim.
 *
 * THE LOOKUP IS NOT REIMPLEMENTED. `RtlQueryEnvironmentVariable` -- the ntdll export the shipped
 * RtlExpandEnvironmentStrings itself calls at 0x1800BB17F -- is resolved live and called with the
 * identical six arguments. Reimplementing it would mean reimplementing the process environment
 * block walk, its cached hash table, its critical section AND the four virtual variables ntdll
 * special-cases ahead of the block (__CD__, __APPDIR__, FIRMWARE_TYPE, NUMBER_OF_PROCESSORS -- read
 * out of ntdll's own table at RVA 0x173AC0 by probes; see RESULTS.md). None of that is the part
 * that is slow, and all of it is the part that is impossible to match by guessing.
 *
 * THE SIX CONTRACT POINTS, all PROVEN against the live export rather than assumed -- every one of
 * them is a direct reading of the disassembly above, and correctness.c re-proves each one on this
 * machine on every run:
 *
 *   return value            produced + 1 -- CHARACTERS, including the terminating null. Returned
 *                           whether or not the output fit.
 *   buffer too small        returns the FULL required length, and last error is NOT touched; the
 *                           destination keeps the nSize-1 characters that did fit and gets NO null
 *                           terminator. (A partial write with no terminator is the documented-wrong
 *                           bit: MSDN says nothing about it.)
 *   an unmatched '%'        copied through literally, and the scan resumes at the NEXT character.
 *   '%%'                    NOT an escape. The first '%' is a literal, the second one opens a new
 *                           name -- so "%%" -> "%%", and "%%TEMP%%" -> "%<value>%".
 *   '%VAR%', VAR unset      copied through literally, all of it, because the leading '%' becomes a
 *                           literal and the name characters are then copied one at a time.
 *   nSize == 0 / lpDst NULL the measuring call: returns the required length, writes nothing, does
 *                           not set last error. lpSrc == NULL behaves as "" and returns 1.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

typedef LONG WIA_NTSTATUS;

#define WIA_STATUS_BUFFER_TOO_SMALL    ((WIA_NTSTATUS)0xC0000023L)
#define WIA_STATUS_VARIABLE_NOT_FOUND  ((WIA_NTSTATUS)0xC0000100L)

typedef WIA_NTSTATUS (NTAPI *wia_qenv_fn)(PVOID, PCWSTR, SIZE_T, PWSTR, SIZE_T, SIZE_T*);

/* The degenerate stand-in used only if ntdll ever stopped exporting the lookup. It reports every
 * name as unset, which is the branch that copies the text through literally -- i.e. the function
 * still terminates and still returns a sane length. impl.asm installs exactly the same stub for
 * exactly the same reason, so the two agree even in that impossible case. */
static WIA_NTSTATUS NTAPI wia_qenv_stub(PVOID e, PCWSTR n, SIZE_T nl, PWSTR v, SIZE_T vl, SIZE_T* rl)
{
    (void)e; (void)n; (void)nl; (void)v; (void)vl;
    if (rl) *rl = 0;
    return WIA_STATUS_VARIABLE_NOT_FOUND;
}

wia_qenv_fn wia_ref_qenv(void)
{
    static wia_qenv_fn cached;
    if (!cached) {
        HMODULE h = GetModuleHandleW(L"ntdll.dll");
        wia_qenv_fn f = h ? (wia_qenv_fn)GetProcAddress(h, "RtlQueryEnvironmentVariable") : NULL;
        cached = f ? f : wia_qenv_stub;
    }
    return cached;
}

DWORD wia_expand_env_w_ref(const wchar_t* lpSrc, wchar_t* lpDst, DWORD nSize)
{
    wia_qenv_fn   query     = wia_ref_qenv();
    size_t        srcLen    = 0;
    size_t        i         = 0;
    size_t        produced  = 0;        /* ntdll's rbp: characters the answer would contain     */
    size_t        dstRemain = nSize;    /* ntdll's r12: characters still free in lpDst          */
    wchar_t*      out       = lpDst;    /* ntdll's r13                                          */
    WIA_NTSTATUS  status    = 0;        /* ntdll's r14d: 0 or STATUS_BUFFER_TOO_SMALL, nothing else */
    unsigned __int64 ret;

    if (lpSrc) { while (lpSrc[srcLen] != 0) ++srcLen; }      /* kernelbase's wcslen */

    while (i < srcLen) {
        if (lpSrc[i] == L'%') {
            /* Characters strictly after this '%'. ntdll's `rax = rdi - 1`. */
            size_t after = srcLen - i - 1;
            if (after != 0) {
                /* Offset of the closing '%', or `after` if there is none. ntdll's r15. */
                size_t k = 0;
                while (k < after && lpSrc[i + 1 + k] != L'%') ++k;

                /* k == 0      -> "%%": an EMPTY name. Not a lookup, not an escape: literal.
                 * k >= after  -> no closing '%' before the end of the string:      literal. */
                if (k != 0 && k < after) {
                    SIZE_T       outLen = 0;
                    WIA_NTSTATUS st = query(NULL, lpSrc + i + 1, k, out, dstRemain, &outLen);

                    if (st >= 0 || st == WIA_STATUS_BUFFER_TOO_SMALL) {
                        if (st == WIA_STATUS_BUFFER_TOO_SMALL) {
                            /* On overflow the lookup reports the length INCLUDING its null, so
                             * the value itself is one less. The cursor does NOT advance: the
                             * lookup wrote at most a single null of its own. */
                            produced += (size_t)outLen - 1;
                            status = st;
                        } else {
                            produced  += (size_t)outLen;
                            out       += outLen;
                            dstRemain -= outLen;
                        }
                        i += k + 2;              /* past '%', the name, and the closing '%' */
                        continue;
                    }
                    /* Any other failure (STATUS_VARIABLE_NOT_FOUND is the usual one) falls
                     * through to the literal copy of the '%' -- and ONLY of the '%'. */
                }
            }
        }

        /* Literal character. Note the asymmetry that makes the truncated output shape what it is:
         * once the status is an error no character is copied any more, but every character is
         * still COUNTED, and the lookup above still runs. */
        if (status >= 0) {
            if (dstRemain <= 1) status = WIA_STATUS_BUFFER_TOO_SMALL;
            else { *out++ = lpSrc[i]; --dstRemain; }
        }
        ++produced;
        ++i;
    }

    if (status >= 0) {
        if (dstRemain == 0) status = WIA_STATUS_BUFFER_TOO_SMALL;   /* nSize == 0 lands here */
        else *out = 0;
    }

    /* kernelbase's wrapper. status is only ever 0 or STATUS_BUFFER_TOO_SMALL, and the wrapper
     * returns the length for both, so its BaseSetLastNTError path is unreachable from here. The
     * 32-bit truncation check is not: it needs a source string of more than 4 billion characters,
     * which is why it is written and not tested. */
    ret = (unsigned __int64)produced + 1;
    if (ret > 0xFFFFFFFFull) { SetLastError(ERROR_GEN_FAILURE); return 0; }
    return (DWORD)ret;
}
