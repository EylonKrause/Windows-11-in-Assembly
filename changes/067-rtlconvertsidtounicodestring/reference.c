/* changes/067-rtlconvertsidtounicodestring/reference.c
 *
 * The scalar oracle for ntdll!RtlConvertSidToUnicodeString, written to be obviously right rather
 * than fast: one division per digit, a scratch array, and a reversal. It exists so that the
 * assembly is compared against something OTHER than the export it is imitating -- if both the
 * assembly and the live export were wrong in the same way, only a third opinion would say so.
 *
 * THE RULES, every one of them measured rather than read (changes/270-convertsidtostringsid/probes):
 *
 *   * "S-" + revision + "-" + identifier authority + ("-" + sub-authority) x count.
 *   * The revision must be 1. Anything else is STATUS_INVALID_SID.
 *   * The sub-authority count must be at most 15, and this line was missing from the first version
 *     of this file and from the first version of impl.asm. The corpus that validated them drew its
 *     count as `(seed>>8)%16`, so a count above 15 was never once generated -- and both of them
 *     would have formatted all 200 sub-authorities of a SID whose count byte said 200, reading
 *     1028 bytes out of a 68-byte structure and writing about 2500 bytes into a 400-byte stack
 *     temporary. ConvertStringSidToSidW accepts 254 sub-authorities, so producing such a SID takes
 *     one call. The live export refuses every count above 15 with STATUS_INVALID_SID.
 *   * The 48-bit identifier authority is DECIMAL when it fits in 32 bits, and otherwise "0x"
 *     followed by minimal UPPERCASE hexadecimal. The boundary is exactly 2^32: 4294967295 prints
 *     as decimal and 0x100000000 prints as hex.
 *   * Each sub-authority is an unsigned 32-bit decimal, with no padding and no grouping.
 *   * On success the string and a terminating NUL are written and Out->Length is set to the byte
 *     count WITHOUT the NUL. MaximumLength must be at least Length+2, else STATUS_BUFFER_OVERFLOW
 *     and out is left completely untouched -- neither Length nor a single byte of the buffer.
 */

typedef long NTSTATUS;
typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } U;

#define ST_INVALID_SID       ((NTSTATUS)0xC0000078)
#define ST_BUFFER_OVERFLOW   ((NTSTATUS)0x80000005)

static const char HX[] = "0123456789ABCDEF";

static unsigned short* du(unsigned short* p, unsigned v)
{
    unsigned short r[12];
    int n = 0;
    do { r[n++] = (unsigned short)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = r[--n];
    return p;
}

NTSTATUS ref_sidfmt(U* out, const unsigned char* sid)
{
    unsigned rev = sid[0], cnt = sid[1];
    unsigned short tmp[400];
    unsigned short* p = tmp;
    unsigned long long auth = 0;
    unsigned i, len;

    if (rev != 1)  return ST_INVALID_SID;
    if (cnt > 15)  return ST_INVALID_SID;

    *p++ = 'S'; *p++ = '-';
    p = du(p, rev);
    *p++ = '-';

    for (i = 0; i < 6; ++i) auth = (auth << 8) | sid[2 + i];
    if (auth < 0x100000000ULL) {
        p = du(p, (unsigned)auth);
    } else {
        unsigned short r[16];
        int n = 0;
        unsigned long long a = auth;
        *p++ = '0'; *p++ = 'x';
        do { r[n++] = (unsigned short)HX[a & 0xF]; a >>= 4; } while (a);
        while (n) *p++ = r[--n];
    }

    for (i = 0; i < cnt; ++i) {
        const unsigned char* q = sid + 8 + 4 * i;
        unsigned sa = (unsigned)q[0] | ((unsigned)q[1] << 8) | ((unsigned)q[2] << 16) |
                      ((unsigned)q[3] << 24);
        *p++ = '-';
        p = du(p, sa);
    }

    /* The room rule is Length+1, not Length+2, and at exactly Length+1 there is no terminator.
       probes/oddroom.c measured it at every MaximumLength around the boundary, for three lengths:
       at Length+1 the string and Out->Length are written and the two bytes past the string keep the
       caller's fill; at Length+2 and above a full wide NUL appears. Both this file and impl.asm
       previously required Length+2, and the old gate could not see it because its overflow sweep
       stepped MaximumLength BY TWO -- every odd value, which is to say the whole boundary, was
       skipped. */
    len = (unsigned)((p - tmp) * 2);
    if (out->MaximumLength < len + 1) return ST_BUFFER_OVERFLOW;
    for (i = 0; i < len / 2; ++i) out->Buffer[i] = tmp[i];
    if (out->MaximumLength >= len + 2) out->Buffer[len / 2] = 0;
    out->Length = (unsigned short)len;
    return 0;
}
