/* changes/273-inet-addr/probes/bytes.c
 *
 * THE BYTE SETS AND THE OVERFLOW BOUNDARIES, SWEPT RATHER THAN SAMPLED.
 *
 * probes/grammar.c established the four forms and the three bases, and turned up three things that
 * a hand-written case list can only hint at:
 *
 *     " 1.2.3.4"    REFUSED        a LEADING space
 *     "1.2.3.4 "    ACCEPTED       a TRAILING space
 *     "1.2.3.4\t"   ACCEPTED       ... and a tab, and a newline
 *     "1.2.3.4x"    REFUSED        but not a letter
 *
 * So there is a set of characters that may follow a complete address and a set that may not, and
 * "whitespace" is a guess at its shape. Change 269 spent a probe on exactly this question for
 * ConvertStringSidToSid and found the answer was 25 code units in one field and none in another --
 * and it found three characters that a hand-written list would never contain. So every byte
 * 0x01..0xFF is asked in every position it can occupy: leading, trailing, between the digits of a
 * part, and as a separator.
 *
 * AND THE OVERFLOW BOUNDARIES, PER FORM. grammar.c showed the export REFUSES rather than wraps:
 *
 *     "1.1.65535"     accepted        "1.1.65536"     refused
 *     "1.16777215"    accepted        "1.16777216"    refused
 *     "4294967295"    refused         -- but that value IS INADDR_NONE, which is ambiguous
 *
 * The last line is the one that matters: a one-part address of 0xFFFFFFFF returns the same 32 bits
 * as a refusal, so "refused" and "accepted" are indistinguishable there BY CONSTRUCTION. What is
 * not ambiguous is 0xFFFFFFFE, and whether 0x100000000 is refused. Each form's boundary is asked
 * from both sides, in decimal, octal and hexadecimal, because a parser that accumulates in 64 bits
 * and checks at the end behaves differently from one that checks as it goes.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

static int accepted(const char* s) { return inet_addr(s) != INADDR_NONE; }

static void sweep(const char* name, const char* fmt)
{
    char s[64];
    int b, n = 0, first = -1;
    char list[1024];
    list[0] = 0;
    for (b = 1; b <= 255; ++b) {
        wsprintfA(s, fmt, b);
        if (accepted(s)) {
            ++n;
            if (first < 0) first = b;
            if (lstrlenA(list) < 900) {
                char t[16];
                wsprintfA(t, "%02X ", b);
                lstrcatA(list, t);
            }
        }
    }
    printf("  %-34s %3d byte(s) accepted%s%s\n", name, n, n ? ":  " : "", n ? list : "");
}

int main(void)
{
    WSADATA wd;
    setvbuf(stdout, NULL, _IONBF, 0);
    WSAStartup(MAKEWORD(2, 2), &wd);

    printf("== 1. every byte 0x01..0xFF, in every position it can occupy ==\n");
    sweep("leading, before the address",  "%c1.2.3.4");
    sweep("trailing, after the address",  "1.2.3.4%c");
    sweep("trailing, after a 1-part form", "1%c");
    sweep("trailing, after a 2-part form", "1.2%c");
    sweep("trailing, after a 3-part form", "1.2.3%c");
    sweep("inside the first part",        "1%c2.3.4.5");
    sweep("inside the last part",         "1.2.3.4%c5");
    sweep("in place of the first dot",    "1%c2.3.4");
    sweep("in place of the last dot",     "1.2.3%c4");
    sweep("as the whole string",          "%c");
    sweep("after a trailing space",       "1.2.3.4 %c");
    sweep("before a trailing space",      "1.2.3.4%c ");

    printf("\n== 2. what two trailing characters are accepted? ==\n");
    {
        int a, b, n = 0;
        char s[32];
        for (a = 1; a <= 255; ++a)
            for (b = 1; b <= 255; ++b) {
                wsprintfA(s, "1.2.3.4%c%c", a, b);
                if (accepted(s)) {
                    if (n < 24) printf("   \"1.2.3.4\" + %02X %02X\n", a, b);
                    ++n;
                }
            }
        printf("   %d pair(s) accepted out of 65025\n", n);
    }

    printf("\n== 3. the digits each base accepts, per part ==\n");
    sweep("first part, decimal",      "%c.2.3.4");
    sweep("first part, after 0",      "0%c.2.3.4");
    sweep("first part, after 0x",     "0x%c.2.3.4");
    sweep("second part, after 0",     "1.0%c.3.4");
    sweep("second part, after 0x",    "1.0x%c.3.4");

    printf("\n== 4. the overflow boundary of each form, from both sides ==\n");
    {
        static const struct { const char* s; } CASES[] = {
            /* one part */
            {"4294967293"}, {"4294967294"}, {"4294967295"}, {"4294967296"}, {"4294967297"},
            {"0xFFFFFFFD"}, {"0xFFFFFFFE"}, {"0xFFFFFFFF"}, {"0x100000000"},
            {"037777777775"}, {"037777777776"}, {"037777777777"}, {"040000000000"},
            /* two parts: the second is 24 bits */
            {"1.16777214"}, {"1.16777215"}, {"1.16777216"},
            {"1.0xFFFFFF"}, {"1.0x1000000"},
            {"1.077777777"}, {"1.0100000000"},
            /* three parts: the third is 16 bits */
            {"1.2.65534"}, {"1.2.65535"}, {"1.2.65536"},
            {"1.2.0xFFFF"}, {"1.2.0x10000"},
            {"1.2.0177777"}, {"1.2.0200000"},
            /* four parts: each is 8 bits */
            {"1.2.3.254"}, {"1.2.3.255"}, {"1.2.3.256"},
            {"1.2.3.0xFF"}, {"1.2.3.0x100"},
            {"1.2.3.0377"}, {"1.2.3.0400"},
            {"254.2.3.4"}, {"255.2.3.4"}, {"256.2.3.4"},
            /* and 255.255.255.255, whose VALUE is the failure signal */
            {"255.255.255.255"}, {"255.255.255.254"}
        };
        unsigned i;
        for (i = 0; i < sizeof CASES / sizeof CASES[0]; ++i) {
            unsigned long v = inet_addr(CASES[i].s);
            printf("   %-20s -> %08lX  %s\n", CASES[i].s, (unsigned long)v,
                   v == INADDR_NONE ? "REFUSED (or is 255.255.255.255)" : "accepted");
        }
    }

    printf("\n== 5. leading zeros, and how many digits a part may have ==\n");
    {
        char s[512];
        int k;
        for (k = 1; k <= 40; k += 4) {
            int i;
            for (i = 0; i < k; ++i) s[i] = '0';
            lstrcpyA(s + k, "1.2.3.4");
            printf("   %2d leading zero(s) on the first part -> %08lX\n", k,
                   (unsigned long)inet_addr(s));
        }
        for (k = 1; k <= 12; ++k) {
            int i;
            lstrcpyA(s, "0x");
            for (i = 0; i < k; ++i) s[2 + i] = '1';
            s[2 + k] = 0;
            printf("   0x with %2d digit(s) -> %08lX\n", k, (unsigned long)inet_addr(s));
        }
    }

    printf("\n== 6. how long a string does it read, and does it stop at the first failure? ==\n");
    {
        static char big[1000005];
        int k;
        for (k = 0; k < 1000000; ++k) big[k] = '1';
        big[1000000] = 0;
        printf("   a million '1' digits            -> %08lX\n", (unsigned long)inet_addr(big));
        lstrcpyA(big, "999.");
        for (k = 4; k < 1000000; ++k) big[k] = '1';
        big[1000000] = 0;
        printf("   \"999.\" + a million digits       -> %08lX  (refusable on the first part)\n",
               (unsigned long)inet_addr(big));
    }
    return 0;
}
