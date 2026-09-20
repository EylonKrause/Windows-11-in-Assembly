/* changes/280-rtllargeintegertochar/reference.c
 *
 * The scalar model: ntdll!RtlLargeIntegerToChar written the slow obvious way, in C, with a
 * divide-per-digit loop and a scratch buffer that is reversed at the end.
 *
 * It exists so the gate is THREE-WAY. Comparing an implementation only against the live export
 * proves it matches Windows; comparing it also against a model written from the measured contract
 * proves the contract was read correctly in the first place. Change 269's gate agreed with live on
 * every case and was still blind, twice over, because the thing it failed to compare was not in
 * either side.
 *
 * Every rule here came from probes/contract.c, and the two that are easy to get wrong are the ones
 * changes 097 and 100 both got wrong:
 *
 *   * `length` is signed. a negative one is a zero-padded field width of exactly that many
 *     characters, with NO terminator. It is not room and it is not an error.
 *   * INT_MIN is the one negative length that refuses, because it cannot be negated.
 *
 * And the one that is easy to miss: the value is UNSIGNED even though the parameter is a signed
 * LARGE_INTEGER, so -1 formats as 18446744073709551615.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG NTSTATUS;

#define ST_INVALID  ((NTSTATUS)0xC000000Dul)
#define ST_OVERFLOW ((NTSTATUS)0x80000005ul)

NTSTATUS ref_lint2char(const LARGE_INTEGER* pv, ULONG base, LONG length, char* out)
{
    unsigned long long value;
    char scratch[80];
    int n = 0, width, i;

    /* the base is validated BEFORE the value pointer is dereferenced -- probes/contract.c measured
       that order by putting the LARGE_INTEGER on a NOACCESS page */
    if (base == 0) base = 10;
    if (base != 2 && base != 8 && base != 10 && base != 16) return ST_INVALID;

    value = (unsigned long long)pv->QuadPart;      /* UNSIGNED, despite the signed parameter */

    do {
        unsigned d = (unsigned)(value % base);
        scratch[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        value /= base;
    } while (value);                                /* zero is "0", one digit, in every base */

    if (length == 0) return ST_OVERFLOW;

    if (length < 0) {
        if (length == (LONG)0x80000000ul) return ST_OVERFLOW;   /* cannot be negated */
        width = -length;
        if (n > width) return ST_OVERFLOW;
        for (i = 0; i < width - n; ++i) out[i] = '0';           /* the field, zero-padded */
        for (i = 0; i < n; ++i) out[width - n + i] = scratch[n - 1 - i];
        return 0;                                               /* and NO terminator */
    }

    if (n > length) return ST_OVERFLOW;
    for (i = 0; i < n; ++i) out[i] = scratch[n - 1 - i];
    if (n < length) out[n] = 0;                                 /* only if it fits */
    return 0;
}
