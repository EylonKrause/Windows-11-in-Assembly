/* changes/265-rtlappendasciiztostring/reference.c
 *
 * The independent oracle for RtlAppendAsciizToString.
 *
 * It shares nothing with impl.asm but the contract. impl.asm scans with a page-safe AVX2 strlen and
 * copies 32 bytes at a time; this counts one byte and copies one byte.
 *
 * The rules, as probes/contract.c measured them, and two of them differ from the wide analogue
 * that change 101 landed, which is why none of them were inherited:
 *
 *   * src == NULL is STATUS_SUCCESS with nothing changed, and so is an empty source.
 *   * It fits if Length + strlen(src) <= MaximumLength, with NO allowance for a terminator: 3 + 3
 *     into MaximumLength 6 succeeds.
 *   * No terminator is ever written. The wide form appends a NUL when there is room; this one does
 *     not, at any size.
 *   * The sum is computed wide. Length 40000 with a 30000-byte source is refused; a 16-bit
 *     comparison would wrap to 4464 and overrun the buffer.
 *   * On failure (STATUS_BUFFER_TOO_SMALL, 0xC0000023) nothing at all is touched.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } REF_ASTR;

LONG ref_appendasciiztostring(void* destv, const char* src)
{
    REF_ASTR* d = (REF_ASTR*)destv;
    SIZE_T n = 0, i;
    if (!src) return 0;
    while (src[n]) ++n;
    if ((SIZE_T)d->Length + n > (SIZE_T)d->MaximumLength)      /* wide, deliberately */
        return (LONG)0xC0000023;                               /* STATUS_BUFFER_TOO_SMALL */
    for (i = 0; i < n; ++i) d->Buffer[d->Length + i] = src[i];
    d->Length = (USHORT)(d->Length + n);
    return 0;                                                  /* and no terminator, ever */
}
