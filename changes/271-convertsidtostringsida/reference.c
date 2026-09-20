/* changes/271-convertsidtostringsida/reference.c
 *
 * The scalar model for advapi32!ConvertSidToStringSidA. It exists so that the assembly is compared
 * against something OTHER than the export it is imitating.
 *
 * The contract is the wide FORM's, with two changes, and probes/contract.c measured all of it by
 * asking both exports the same question over every shape of SID:
 *
 *   * the characters are the wide form's, narrowed one byte per character. Zero differences over
 *     every count 0..255, every revision 0..255, the identifier authority at every decimal and
 *     hexadecimal boundary at six counts -- and zero again under four thread locales including
 *     Shift-JIS and UTF-8, so there is no code page in this;
 *   * the block is characters + 1 BYTES rather than (characters + 1) * 2.
 *
 * Everything else is identical and was checked rather than inherited: ERROR_INVALID_PARAMETER for a
 * NULL argument, ERROR_INVALID_SID for anything the formatter refuses, the output pointer LEFT
 * ALONE on failure, the last error ZEROED on success, and LocalAlloc(LMEM_FIXED).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef long NTSTATUS;
typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } U;
NTSTATUS ref_sidfmt(U*, const unsigned char*);      /* change 067's model, linked in */

int ref_sid2stra(const unsigned char* sid, char** out)
{
    unsigned short tmp[400];
    U u;
    NTSTATUS st;
    unsigned chars, i;
    char* p;

    if (!sid || !out) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }

    u.Length = 0;
    u.MaximumLength = sizeof tmp;
    u.Buffer = tmp;
    st = ref_sidfmt(&u, sid);
    if (st != 0) { SetLastError(ERROR_INVALID_SID); return 0; }

    chars = u.Length / 2u;
    p = (char*)LocalAlloc(LMEM_FIXED, chars + 1);
    if (!p) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    for (i = 0; i < chars; ++i) p[i] = (char)(unsigned char)tmp[i];
    p[chars] = 0;
    *out = p;
    SetLastError(0);
    return 1;
}
