/* changes/273-inet-addr/reference.c
 *
 * The scalar model for ws2_32!inet_addr, written from what four probes measured rather than from
 * the BSD source everyone quotes, the two disagree in three places.
 *
 * THE GRAMMAR (probes/grammar.c). Four forms, and each part is decimal, octal after a leading `0`,
 * or hexadecimal after `0x`/`0X`. The base is decided PER PART: `1.0x2.03.4` is legal.
 *
 *     a.b.c.d   four 8-bit fields
 *     a.b.c     c is 16 bits in the low two bytes
 *     a.b       b is 24 bits in the low three
 *     a         a is 32 bits
 *
 * The result is returned in NETWORK byte order.
 *
 * ------------------------------------------------------------------------------------------------
 * Three things that are not what the source everyone quotes does
 *
 * 1. The overflow test is "did the accumulator go down", not a range check (probes/accum.c).
 *    The accumulator is 32 bits and wraps; a digit is refused only when the wrapped result is
 *    STRICTLY LESS than the value before it. That is not the textbook test, and the difference is
 *    observable on inputs anyone could type:
 *
 *        0x112345678    accepted, 0x12345678, the textbook check `acc > (MAX-d)/16` refuses it
 *        0x212345678    REFUSED, 0x12345678 < 0x21234567, so it goes down
 *        12345678901    accepted, 0xDFDC1C35, wrapped, and larger than 1234567890
 *        99999999999    REFUSED, wrapped to something smaller
 *        0x7FFFFFFF0    accepted, 0xFFFFFFF0, overflows the 32 bits and is still accepted
 *
 *    A nine-digit hexadecimal number is accepted if and only if its leading digit is 0 or 1, and
 *    probes/accum.c swept all sixteen to establish that. An implementation with the "correct" check
 *    is wrong on the first and third lines.
 *
 * 2. Whitespace ends the address and everything after it is ignored, but only once a digit has
 *    been consumed. Any of the six bytes 09 0A 0B 0C 0D 20 does it, and the rest of the string is
 *    never looked at: "1.2.3.4 junk" is 1.2.3.4 and "1 junk" is 0.0.0.1. Leading whitespace is
 *    refused. probes/bytes.c swept every byte in every position to get that set.
 *
 * 3. The single byte 0x20 is an address. `" "` (one space and a terminator, nothing else) comes
 *    back as 0.0.0.0. Two spaces do not. A tab does not. `" 1"` does not. `""` does not.
 *    probes/lonespace.c asked it from every side, and no model of the grammar explains it: it is
 *    one input out of all possible inputs, and it is reproduced here because a gate that compares
 *    against the live export would otherwise report it forever.
 *
 * And one ambiguity that is built in. INADDR_NONE is 0xFFFFFFFF, which is also the value of
 * 255.255.255.255, so a refusal and that one address are indistinguishable, to this model, to the
 * assembly, and to every caller. probes/grammar.c confirmed the last error is not set either way.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

#define NONE 0xFFFFFFFFul

static int dig(int c, int base)
{
    if (c >= '0' && c <= '9') { int v = c - '0'; return v < base ? v : -1; }
    if (base != 16) return -1;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int is_ws(int c)
{
    return c == 0x09 || c == 0x0A || c == 0x0B || c == 0x0C || c == 0x0D || c == 0x20;
}

unsigned long ref_inet_addr(const char* s)
{
    const unsigned char* p = (const unsigned char*)s;
    unsigned long part[4];
    unsigned long v;
    int n = 0;

    if (!s) return NONE;
    if (p[0] == 0x20 && p[1] == 0) return 0;     /* the one special case; see the header */

    for (;;) {
        unsigned long acc = 0;
        int base = 10, any = 0, d;

        if (dig(p[0], 10) < 0) return NONE;      /* a part must start with a decimal digit */
        if (p[0] == '0') {
            ++p;
            if (p[0] == 'x' || p[0] == 'X') {
                ++p;
                base = 16;
                if (dig(p[0], 16) < 0) return NONE;   /* "0x" alone is not a number */
            } else {
                base = 8;
                any = 1;                         /* the leading zero IS the digit */
            }
        }
        while ((d = dig(p[0], base)) >= 0) {
            unsigned long nw = acc * (unsigned long)base + (unsigned long)d;
            if (nw < acc) return NONE;           /* the measured test: it went DOWN */
            acc = nw;
            any = 1;
            ++p;
        }
        if (!any) return NONE;
        if (n == 4) return NONE;
        part[n++] = acc;

        if (p[0] != '.') break;
        ++p;
    }

    if (p[0] != 0 && !is_ws(p[0])) return NONE;

    switch (n) {
    case 1:
        v = part[0];
        break;
    case 2:
        if (part[0] > 0xFF || part[1] > 0xFFFFFF) return NONE;
        v = (part[0] << 24) | part[1];
        break;
    case 3:
        if (part[0] > 0xFF || part[1] > 0xFF || part[2] > 0xFFFF) return NONE;
        v = (part[0] << 24) | (part[1] << 16) | part[2];
        break;
    default:
        if (part[0] > 0xFF || part[1] > 0xFF || part[2] > 0xFF || part[3] > 0xFF) return NONE;
        v = (part[0] << 24) | (part[1] << 16) | (part[2] << 8) | part[3];
        break;
    }
    /* network byte order */
    return ((v & 0xFFul) << 24) | ((v & 0xFF00ul) << 8) | ((v >> 8) & 0xFF00ul) | ((v >> 24) & 0xFFul);
}
