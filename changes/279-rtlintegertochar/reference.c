/* changes/279-rtlintegertochar/reference.c
 *
 * The scalar model for ntdll!RtlIntegerToChar: a division per digit into a scratch, then copied
 * out. It exists so the assembly is compared against something that does not use the length-first
 * trick.
 *
 * THE RULES, all measured:
 *
 *   * bases 0, 2, 8, 10, 16 only, 0 means 10, everything else STATUS_INVALID_PARAMETER
 *     (probes/contract.c asked 0..20 one at a time, then 36 and 0xFFFFFFFF);
 *   * a POSITIVE length is room in BYTES: the call needs length >= digits, and a terminator is
 *     written only if length > digits. That is change 067's rule, and NOT change 278's, which
 *     demands Length+2 and always terminates -- three formatters in one DLL, two rules;
 *   * a NEGATIVE length is a ZERO-PADDED FIELD WIDTH of -length characters, with NO terminator:
 *     -11 on a ten-digit number gives "03735928559". probes/negative.c found that by sweeping every
 *     negative length against a guard page. INT_MIN refuses, because it cannot be negated;
 *   * length 0 refuses;
 *   * the value is unsigned, and a refusal leaves the buffer untouched.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG NTSTATUS;
#define ST_INVALID  ((NTSTATUS)0xC000000Dul)
#define ST_OVERFLOW ((NTSTATUS)0x80000005ul)

NTSTATUS ref_int2char(ULONG value, ULONG base, LONG length, char* out)
{
    char scratch[40];
    int n = 0, width, i;

    if (base == 0) base = 10;
    if (base != 2 && base != 8 && base != 10 && base != 16) return ST_INVALID;

    do {
        unsigned d = (unsigned)(value % base);
        scratch[n++] = (char)(d < 10 ? ('0' + d) : ('A' + d - 10));
        value /= base;
    } while (value);

    if (length == 0) return ST_OVERFLOW;
    if (length < 0) {
        if (length == (LONG)0x80000000ul) return ST_OVERFLOW;
        width = -length;
        if (n > width) return ST_OVERFLOW;
        for (i = 0; i < width - n; ++i) out[i] = '0';
        for (i = 0; i < n; ++i) out[width - n + i] = scratch[n - 1 - i];
        return 0;                                  /* no terminator */
    }
    if (n > length) return ST_OVERFLOW;
    for (i = 0; i < n; ++i) out[i] = scratch[n - 1 - i];
    if (n < length) out[n] = 0;
    return 0;
}
