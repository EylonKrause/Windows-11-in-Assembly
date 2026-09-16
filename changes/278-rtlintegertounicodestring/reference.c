/* changes/278-rtlintegertounicodestring/reference.c
 *
 * The scalar model for ntdll!RtlIntegerToUnicodeString, written to be obviously right rather than
 * fast: a division per digit into a scratch, then reversed out. It exists so that the assembly is
 * compared against something OTHER than the export it is imitating -- and, here, so that the
 * length-first trick is compared against a converter that does not use it.
 *
 * THE RULES, all measured in probes/contract.c:
 *
 *   * bases 0, 2, 8, 10 and 16 only, and 0 means 10; every other value 1..0xFFFFFFFF is
 *     STATUS_INVALID_PARAMETER -- the probe asked 0..20 one at a time rather than trusting the
 *     documented set, and then 36, 256 and 0xFFFFFFFF;
 *   * no prefix, uppercase hexadecimal digits, and zero is "0" in every base;
 *   * MaximumLength must be at least Length + 2 -- NOT Length + 1, which is what change 067 found
 *     for RtlConvertSidToUnicodeString in the same DLL;
 *   * a terminator is always written and Length excludes it;
 *   * on ANY failure the destination is completely untouched, Length included;
 *   * the value is unsigned.
 *
 * The base is checked BEFORE the room, which probes/contract.c established by asking for a bad base
 * with ample room and an impossible room with a good base, and which correctness.c then asks with
 * both wrong at once.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;

#define ST_INVALID  ((NTSTATUS)0xC000000Dul)
#define ST_OVERFLOW ((NTSTATUS)0x80000005ul)

NTSTATUS ref_int2ustr(ULONG value, ULONG base, USTR* out)
{
    wchar_t scratch[40];
    int n = 0;
    unsigned need;
    int i;

    if (base == 0) base = 10;
    if (base != 2 && base != 8 && base != 10 && base != 16) return ST_INVALID;

    do {
        unsigned d = (unsigned)(value % base);
        scratch[n++] = (wchar_t)(d < 10 ? (L'0' + d) : (L'A' + d - 10));
        value /= base;
    } while (value);

    need = (unsigned)n * 2u + 2u;
    if (out->MaximumLength < need) return ST_OVERFLOW;

    for (i = 0; i < n; ++i) out->Buffer[i] = scratch[n - 1 - i];
    out->Buffer[n] = 0;
    out->Length = (USHORT)(n * 2);
    return 0;
}
