/* changes/273-inet-addr/probes/overflow.c
 *
 * How does the accumulator overflow? The previous probe could not tell.
 *
 * probes/bytes.c asked what "0x" followed by k digits returns, and built the digits out of ONES:
 *
 *     0x with  8 digit(s) -> 11111111
 *     0x with  9 digit(s) -> 11111111
 *     0x with 12 digit(s) -> 11111111
 *
 * which looks like proof that the accumulator wraps at 32 bits. It is not proof of anything. The
 * value 0x11111111 has all four bytes equal, and inet_addr returns NETWORK byte order, so the
 * answer reads the same whether the parser wrapped, truncated, saturated or byte-swapped. It was a
 * test whose input could not distinguish the hypotheses -- the same defect as change 067's corpus
 * stepping MaximumLength by two, in a different costume.
 *
 * So this file asks again with values whose four bytes are all DIFFERENT, and separates the three
 * things that could be happening:
 *
 *     WRAP        the low 32 bits are kept and the call succeeds
 *     REFUSE      anything above 0xFFFFFFFF is INADDR_NONE
 *     TRUNCATE    only the first N digits are consumed and the rest ignored
 *
 * And it asks the same question of decimal and octal, because a parser that checks as it goes and
 * one that accumulates in 64 bits and checks at the end differ exactly here.
 *
 * It also settles what LEADING ZEROS do to the count, since probes/bytes.c showed 37 of them are
 * tolerated -- so "at most N digits" cannot be the rule as stated.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

/* inet_addr returns network byte order; print the value the way the string names it */
static unsigned long hostval(unsigned long net)
{
    unsigned char* b = (unsigned char*)&net;
    return ((unsigned long)b[0] << 24) | ((unsigned long)b[1] << 16) |
           ((unsigned long)b[2] << 8) | b[3];
}

static void one(const char* s, const char* note)
{
    unsigned long v = inet_addr(s);
    printf("   %-26s -> net %08lX", s, (unsigned long)v);
    if (v == INADDR_NONE) printf("   REFUSED");
    else                  printf("   value 0x%08lX", hostval(v));
    if (note) printf("   %s", note);
    printf("\n");
}

int main(void)
{
    WSADATA wd;
    char s[256];
    setvbuf(stdout, NULL, _IONBF, 0);
    WSAStartup(MAKEWORD(2, 2), &wd);

    printf("== 1. hexadecimal, with four DIFFERENT bytes, one digit past the end ==\n");
    printf("   0x12345678 is the control. If a ninth digit WRAPS, 0x123456789 becomes 0x23456789.\n");
    printf("   If it REFUSES, it is INADDR_NONE. If it TRUNCATES to eight, it stays 0x12345678.\n");
    one("0x12345678",   "the control");
    one("0x123456789",  "nine digits");
    one("0x1234567890", "ten digits");
    one("0x00012345678", "eight significant digits behind three zeros");
    one("0xF0000000",   "the control, high bit set");
    one("0xF00000000",  "nine digits");

    printf("\n== 2. decimal, the same question ==\n");
    printf("   4294967294 is the largest accepted one-part value (0xFFFFFFFE).\n");
    one("4294967294",   "the control");
    one("4294967295",   "= INADDR_NONE, ambiguous by construction");
    one("4294967296",   "2^32");
    one("8589934592",   "2^33 -- wrapping would give 0");
    one("8589934593",   "2^33+1 -- wrapping would give 1");
    one("12884901889",  "3*2^32+1 -- wrapping would give 1");

    printf("\n== 3. octal, the same question ==\n");
    one("037777777776", "the control, 0xFFFFFFFE");
    one("040000000000", "2^32");
    one("0100000000000", "2^35 -- wrapping would give 0");

    printf("\n== 4. and the same for a part that is not the last ==\n");
    one("1.2.3.256",    "one over the 8-bit field");
    one("1.2.3.257",    "two over -- wrapping would give 1");
    one("1.2.3.512",    "2*256 -- wrapping would give 0");
    one("1.2.3.0x100",  "the same in hex");
    one("256.2.3.4",    "the FIRST part, one over");
    one("257.2.3.4",    "wrapping would give 1");

    printf("\n== 5. leading zeros do not count as digits ==\n");
    {
        int k;
        for (k = 0; k <= 3; ++k) {
            int i, n = 0;
            for (i = 0; i < k * 10; ++i) s[n++] = '0';
            lstrcpyA(s + n, "0x12345678");          /* note: the zeros make it octal, then 'x' ... */
            one(s, "zeros then 0x -- the base is decided by the FIRST character");
        }
        for (k = 0; k <= 3; ++k) {
            int i, n = 0;
            s[n++] = '0'; s[n++] = 'x';
            for (i = 0; i < k * 10; ++i) s[n++] = '0';
            lstrcpyA(s + n, "12345678");
            one(s, "0x then zeros then eight significant digits");
        }
        {
            int i, n = 0;
            for (i = 0; i < 200; ++i) s[n++] = '0';
            lstrcpyA(s + n, "1.2.3.4");
            printf("   200 zeros then 1.2.3.4       -> net %08lX\n", (unsigned long)inet_addr(s));
        }
    }

    printf("\n== 6. where exactly does the whitespace rule stop the parse? ==\n");
    printf("   probes/bytes.c found that ANY byte is accepted after a trailing space, which means\n");
    printf("   the parse STOPS at whitespace and ignores the rest. Is that true mid-address too?\n");
    one("1 .2.3.4",    "a space after the first part");
    one("1. 2.3.4",    "a space after the first dot");
    one("1.2.3.4 junk","a space then rubbish");
    one("1.2.3.4\tjunk", "a tab then rubbish");
    one("1 junk",      "a one-part address, a space, then rubbish");
    one(" ",           "a lone space");
    one("  ",          "two spaces");
    one("\t",          "a lone tab");
    one("0 ",          "zero and a space");
    one(".2.3.4",      "a leading dot");

    printf("\n== 7. the one-part form with a trailing dot, and counts above four ==\n");
    one("1.",          0);
    one("1.2.",        0);
    one("1.2.3.4.",    0);
    one("1.2.3.4.5",   0);
    one("1.2.3.4.5.6", 0);
    return 0;
}
