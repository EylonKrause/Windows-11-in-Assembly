/* changes/270-convertsidtostringsid/reference.c
 *
 * The scalar model for advapi32!ConvertSidToStringSidW, written to be obviously right rather than
 * fast. It exists so that the assembly is compared against something OTHER than the export it is
 * imitating: if both were wrong in the same way, only a third opinion would say so.
 *
 * THE CONTRACT, measured by four probes rather than read:
 *
 *   1. The text is exactly ntdll!RtlConvertSidToUnicodeString's. probes/contract.c formats every
 *      shape of SID with both and compares the strings: the ordinary counts, every revision, every
 *      sub-authority count from 0 to 255, and the identifier authority at every decimal and
 *      hexadecimal boundary. They agree on all of it, including on what they REFUSE, a revision
 *      other than 1 and a count above 15. So this change is an ENVELOPE over change 067, the way
 *      change 268 is an envelope over 016 and 034, and not a second copy of a formatter.
 *
 *   2. The failure codes are Win32, not NTSTATUS, and there are only two of them:
 *
 *          a NULL SID or a NULL out-pointer   ERROR_INVALID_PARAMETER  (87)
 *          anything the formatter refuses     ERROR_INVALID_SID        (1337)
 *
 *      That is the whole set. STATUS_INVALID_SID maps to ERROR_INVALID_SID and nothing else does.
 *
 *   3. On failure the output pointer is left alone. Not cleared, left exactly as the caller had
 *      it, which probes/contract.c measured with a poison value. (Its sibling
 *      ConvertStringSidToSidW, change 269, does clear it for three characters out of 65535; this
 *      one never does.)
 *
 *   4. On success the last error is zero, whatever it was before. probes/validate.c asked with five
 *      starting values at four lengths: twenty out of twenty came back 0.
 *
 *   5. The block is a LocalAlloc block of exactly (characters + 1) * 2 Bytes, with LocalFlags 0 --
 *      LMEM_FIXED. Measured at counts 0, 1, 5 and 15; the longest possible result is 183 characters
 *      and therefore a 368-byte block.
 *
 *   6. a SID that is not fully readable is a refusal when its sub-authority array runs off the end
 *      and a FAULT when only its six identifier-authority bytes do (probes/truncated.c). That is
 *      change 067's rule, inherited here because it is the same formatter underneath, and it is
 *      why this model cannot express it: a scalar model cannot fault on demand. The correctness
 *      gate checks that behaviour against the LIVE export directly, with a guard page.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef long NTSTATUS;
typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } U;
NTSTATUS ref_sidfmt(U*, const unsigned char*);      /* change 067's model, linked in */

/* Returns the BOOL. On success *out is a LocalAlloc block the caller frees; on failure *out is not
   written and the last error is set. */
int ref_sid2str(const unsigned char* sid, wchar_t** out)
{
    unsigned short tmp[400];
    U u;
    NTSTATUS st;
    unsigned chars;
    wchar_t* p;

    if (!sid || !out) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }

    u.Length = 0;
    u.MaximumLength = sizeof tmp;
    u.Buffer = tmp;
    st = ref_sidfmt(&u, sid);
    if (st != 0) { SetLastError(ERROR_INVALID_SID); return 0; }

    chars = u.Length / 2u;
    p = (wchar_t*)LocalAlloc(LMEM_FIXED, (chars + 1) * 2);
    if (!p) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    {
        unsigned i;
        for (i = 0; i < chars; ++i) p[i] = (wchar_t)tmp[i];
        p[chars] = 0;
    }
    *out = p;
    SetLastError(0);
    return 1;
}
