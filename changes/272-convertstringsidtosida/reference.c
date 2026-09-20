/* changes/272-convertstringsidtosida/reference.c
 *
 * The scalar model for advapi32!ConvertStringSidToSidA. It exists so that the assembly is compared
 * against something OTHER than the export it is imitating.
 *
 * The model is the measurement: probes/codepage.c established that
 *
 *     ConvertStringSidToSidA(s) == ConvertStringSidToSidW(MultiByteToWideChar(CP_ACP, 0, s, -1, ...))
 *
 * with zero disagreements over every byte 0x01..0xFF in each of six field positions, every one of
 * the 9025 printable ASCII pairs against the alias table, the byte sequences that may not translate
 * at all, and the SDDL terminators that make a FAILING call clear the output pointer.
 *
 * So this file does exactly that, using change 269's wide model underneath, deliberately, because
 * a model that called the live wide EXPORT would make the gate compare advapi32 against advapi32
 * and agree with itself. It widens with the real MultiByteToWideChar, because that IS the contract
 * here; the assembly's ASCII fast path is an optimisation over it that probes/asciilen.c licensed by
 * measuring all 22 code pages, and the whole point of the model is to disagree if the fast path is
 * ever wrong.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>

BOOL ref_str2sid(const wchar_t*, PSID*);       /* change 269's model */

int ref_str2sida(const char* s, void** out)
{
    wchar_t stackbuf[1024];
    wchar_t* w = stackbuf;
    int n, r;

    if (!s || !out) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }

    n = (int)strlen(s) + 1;
    if (n > 1024) {
        w = (wchar_t*)malloc((size_t)n * 2);
        if (!w) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    }
    if (MultiByteToWideChar(CP_ACP, 0, s, n, w, n) <= 0) {
        if (w != stackbuf) free(w);
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    r = ref_str2sid(w, (PSID*)out) ? 1 : 0;
    if (w != stackbuf) {
        DWORD e = GetLastError();
        free(w);
        SetLastError(e);
    }
    return r;
}
