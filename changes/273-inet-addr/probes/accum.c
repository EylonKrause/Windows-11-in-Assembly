/* changes/273-inet-addr/probes/accum.c
 *
 * The two rules probes/overflow.c narrowed but did not settle.
 *
 * ------------------------------------------------------------------------------------------------
 * a. Hexadecimal seems to wrap, except when it does not.
 *
 *     0x12345678     accepted, 0x12345678         eight digits, the control
 *     0x123456789    accepted, 0x23456789         NINE digits -- the top nibble is simply GONE
 *     0x1234567890   accepted, 0x34567890         ten digits, two nibbles gone
 *     0xF00000000    REFUSED                      nine digits, and this one is not accepted at all
 *
 * A plain 32-bit wrapping accumulator explains the first three and not the fourth: 0xF00000000 wraps
 * to zero, which is a perfectly good address. A 64-bit accumulator with a range check explains the
 * fourth and not the second. Neither hypothesis survives all four lines, so a third rule is at work,
 * and guessing which is exactly what this project keeps being punished for.
 *
 * The obvious candidate is that the overflow test looks at ONE BIT rather than at the value: refuse
 * if the accumulator already has its TOP bit set before the shift, and otherwise shift and let the
 * top nibble fall off. That predicts every line above. It also predicts a sharp boundary: a nine
 * digit number is accepted if and only if its leading digit is 0-7, because 8-F set bit 31 of the
 * eight digits that precede the last one. So the leading digit is swept, at nine and ten digits,
 * and the prediction either holds for all fifteen or it is the wrong rule.
 *
 * ------------------------------------------------------------------------------------------------
 * B. a lone space is an address and two spaces are not.
 *
 *     " "      accepted, 0.0.0.0
 *     "  "     REFUSED
 *     "\t"     REFUSED
 *     "1 .2.3.4"   accepted, 0.0.0.1     -- the parse stops at the space and keeps what it has
 *
 * So a space terminates the address and the rest of the string is ignored -- but only one space, and
 * only a space. That is three rules fighting, and the corpus has to contain whichever one is real,
 * because an implementation that gets it wrong is wrong on an input any caller could produce by
 * trimming a string badly. Every combination of the six whitespace bytes is asked, at the front, in
 * the middle and at the end, with and without digits around them.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

static unsigned long hostval(unsigned long net)
{
    unsigned char* b = (unsigned char*)&net;
    return ((unsigned long)b[0] << 24) | ((unsigned long)b[1] << 16) |
           ((unsigned long)b[2] << 8) | b[3];
}

int main(void)
{
    WSADATA wd;
    char s[64];
    int d, k;
    setvbuf(stdout, NULL, _IONBF, 0);
    WSAStartup(MAKEWORD(2, 2), &wd);

    printf("== A. nine hexadecimal digits, sweeping the LEADING one ==\n");
    printf("   prediction: accepted iff the leading digit is 0-7, and the value is the low 32 bits\n");
    printf("   digit  string          result            predicted\n");
    for (d = 0; d <= 15; ++d) {
        unsigned long v, pred;
        int i;
        s[0] = '0'; s[1] = 'x';
        s[2] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        for (i = 0; i < 8; ++i) s[3 + i] = "12345678"[i];
        s[11] = 0;
        v = inet_addr(s);
        /* the low 32 bits of 0x<d>12345678 */
        pred = 0x12345678ul;
        printf("     %X    %-14s  ", d, s);
        if (v == INADDR_NONE) printf("REFUSED          ");
        else                  printf("0x%08lX       ", hostval(v));
        printf("%s\n", (d <= 7) ? "accept, 0x12345678" : "refuse");
        (void)pred;
    }

    printf("\n== A2. ten hexadecimal digits, sweeping the leading one ==\n");
    for (d = 0; d <= 15; ++d) {
        unsigned long v;
        int i;
        s[0] = '0'; s[1] = 'x';
        s[2] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        for (i = 0; i < 9; ++i) s[3 + i] = "123456789"[i];
        s[12] = 0;
        v = inet_addr(s);
        printf("     %X    %-14s  ", d, s);
        if (v == INADDR_NONE) printf("REFUSED\n");
        else                  printf("0x%08lX\n", hostval(v));
    }

    printf("\n== A3. the boundary inside eight digits: is it the top bit or the top nibble? ==\n");
    {
        static const char* CASES[] = {
            "0x7FFFFFFF0", "0x80000000", "0x800000000", "0x7FFFFFFFF",
            "0x0FFFFFFFF", "0x10000000F", "0x8000000",  "0x80000000F"
        };
        unsigned i;
        for (i = 0; i < sizeof CASES / sizeof CASES[0]; ++i) {
            unsigned long v = inet_addr(CASES[i]);
            printf("   %-14s -> ", CASES[i]);
            if (v == INADDR_NONE) printf("REFUSED\n");
            else                  printf("0x%08lX\n", hostval(v));
        }
    }

    printf("\n== A4. does DECIMAL do the same thing one power lower? ==\n");
    {
        static const char* CASES[] = {
            "42949672950", "4294967294", "42949672940", "12345678901", "999999999",
            "9999999999", "99999999999"
        };
        unsigned i;
        for (i = 0; i < sizeof CASES / sizeof CASES[0]; ++i) {
            unsigned long v = inet_addr(CASES[i]);
            printf("   %-14s -> ", CASES[i]);
            if (v == INADDR_NONE) printf("REFUSED\n");
            else                  printf("0x%08lX\n", hostval(v));
        }
    }

    printf("\n== B. the six whitespace bytes, everywhere ==\n");
    {
        static const unsigned char WS[] = { 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x20 };
        static const char* SHAPE[] = {
            "%c", "%c%c", "%c1.2.3.4", "1.2.3.4%c", "1%c.2.3.4", "1.%c2.3.4",
            "1.2.3.4%cx", "%c%c1.2.3.4", "0%c", "1%c2"
        };
        unsigned a, b, f;
        printf("   shape             ");
        for (a = 0; a < 6; ++a) printf(" %02X      ", WS[a]);
        printf("\n");
        for (f = 0; f < sizeof SHAPE / sizeof SHAPE[0]; ++f) {
            printf("   %-18s", SHAPE[f]);
            for (a = 0; a < 6; ++a) {
                unsigned long v;
                wsprintfA(s, SHAPE[f], WS[a], WS[a]);
                v = inet_addr(s);
                if (v == INADDR_NONE) printf(" REFUSED ");
                else                  printf(" %08lX", hostval(v));
            }
            printf("\n");
        }
        (void)b;
    }

    printf("\n== B2. and a space in every position of a four-part address ==\n");
    {
        static const char* BASE = "1.22.33.44";
        int n = (int)strlen(BASE);
        for (k = 0; k <= n; ++k) {
            unsigned long v;
            int i, j = 0;
            for (i = 0; i < k; ++i) s[j++] = BASE[i];
            s[j++] = ' ';
            for (i = k; i < n; ++i) s[j++] = BASE[i];
            s[j] = 0;
            v = inet_addr(s);
            printf("   %-14s -> ", s);
            if (v == INADDR_NONE) printf("REFUSED\n");
            else                  printf("0x%08lX\n", hostval(v));
        }
    }
    return 0;
}
