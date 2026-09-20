/* changes/296-rtlcopyunicodestring/reference.c -- the correctness oracle.
 *
 * VOID NTAPI RtlCopyUnicodeString(UNICODE_STRING* dst, const UNICODE_STRING* src)
 *
 * Deliberately naive, byte at a time. Every rule below was PROVED against the live
 * ntdll export on this machine by probes/contract.c; none of it is taken from the
 * documentation, and two of the rules contradict the obvious reading of it.
 *
 * WHAT WAS PROVED (probes/contract.c output, quoted in RESULTS.md):
 *
 *  1. src == NULL  ->  dst->Length = 0 and NOTHING ELSE happens. dst->Buffer is not
 *     dereferenced, dst->MaximumLength is unchanged, no NUL is written.
 *
 *  2. n = min(src->Length, dst->MaximumLength)   -- a BYTE count, and the clamp is
 *     RAW: it does NOT round down to a whole WCHAR. With MaximumLength = 7 and a
 *     20-byte source, the live export copies SEVEN bytes and reports Length = 7,
 *     leaving half a WCHAR in the destination. (Shipped code: `cmovbe eax,r8d` on
 *     the 16-bit compare, with no `and eax,-2` anywhere.)
 *
 *  3. dst->Length = n, ALWAYS -- including n = 0.
 *
 *  4. dst->MaximumLength is never written.
 *
 *  5. The copy is a MEMMOVE, not a memcpy: the shipped code tail-calls ntdll's
 *     memmove, which tests `src - dst` and runs backwards when the destination is
 *     inside the source. Verified byte-for-byte against C memmove at n = 512 with
 *     dst = src + 8.
 *
 *  6. A terminating wide NUL is written IFF  n + 2 <= dst->MaximumLength, and it is
 *     placed at BYTE offset (n & ~1) -- i.e. at WCHAR index n/2, floored. For an ODD
 *     n that offset is n-1, so the NUL OVERWRITES THE LAST BYTE COPIED. Proved:
 *     src->Length = 1 with room to spare leaves `00 00` in the destination, not
 *     `41 00`. (Shipped code: `shr rbx,1` then `mov [rsi+rbx*2],ax`.)
 *     Note this is the opposite of RtlAppendUnicodeStringToString (change 102),
 *     which writes no NUL at all when the appended length is zero.
 *
 *  7. src->MaximumLength is never read -- a source claiming MaximumLength = 0 with
 *     Length = 8 still copies 8 bytes.
 *
 * OUT OF CONTRACT (matched by neither this reference nor impl.asm, and excluded from
 * the corpus): a dst->Buffer that ALIASES the UNICODE_STRING struct itself. The
 * shipped code re-reads dst->Length and dst->MaximumLength from memory after the
 * copy, so such a call would observe whatever the copy wrote over them. No caller
 * does this and the behaviour is not a documented property.
 */

typedef unsigned short USHORT_;
typedef struct { USHORT_ Length, MaximumLength; unsigned short* Buffer; } REF_US;

void ref_copyus(REF_US* dst, const REF_US* src)
{
    unsigned char* d;
    const unsigned char* s;
    unsigned n, i;

    if (src == 0) {                       /* rule 1 */
        dst->Length = 0;
        return;
    }

    n = (unsigned)src->Length;             /* rule 2: raw byte clamp, no WCHAR rounding */
    if (n > (unsigned)dst->MaximumLength) n = (unsigned)dst->MaximumLength;

    dst->Length = (USHORT_)n;              /* rule 3 */
                                           /* rule 4: MaximumLength untouched */

    d = (unsigned char*)dst->Buffer;       /* rule 5: the textbook obviously-correct memmove */
    s = (const unsigned char*)src->Buffer;
    if (d <= s) { for (i = 0; i < n; ++i) d[i] = s[i]; }
    else        { for (i = n; i-- > 0; )  d[i] = s[i]; }

    if (n + 2u <= (unsigned)dst->MaximumLength) {   /* rule 6 */
        unsigned at = n & ~1u;             /* WCHAR index n/2, floored, in bytes */
        d[at]     = 0;
        d[at + 1] = 0;
    }
}
