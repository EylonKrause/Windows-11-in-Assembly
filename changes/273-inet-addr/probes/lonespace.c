/* changes/273-inet-addr/probes/lonespace.c
 *
 * One space is an address. Two are not. Why?
 *
 * probes/accum.c settled everything about this export except one line:
 *
 *     " "      accepted, 0.0.0.0
 *     "  "     REFUSED
 *     "\t"     REFUSED
 *     " 1.2.3.4"   REFUSED
 *
 * Every model that explains three of those fails on the fourth. "Leading whitespace is skipped"
 * predicts " 1.2.3.4" accepted. "A part must start with a digit" -- which is what the BSD source
 * does -- predicts " " refused. "The parse stops at whitespace and the rest is ignored", which is
 * exactly what "1.2.3.4 junk" shows, predicts "  " accepted.
 *
 * A rule that three models disagree about is a rule to MEASURE, not to pick. This file asks the
 * question from every side: how many spaces, what follows them, whether a digit before them
 * changes it, and whether the other five whitespace bytes ever behave like the space does.
 *
 * It matters because the answer is a branch in the implementation, and because an input of " " is
 * not exotic -- it is what a caller passes after trimming a field badly.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

static void one(const char* s, const char* show)
{
    unsigned long v = inet_addr(s);
    printf("   %-22s -> ", show);
    if (v == INADDR_NONE) printf("REFUSED\n");
    else {
        unsigned char* b = (unsigned char*)&v;
        printf("%u.%u.%u.%u\n", b[0], b[1], b[2], b[3]);
    }
}

int main(void)
{
    WSADATA wd;
    char s[64];
    int k;
    setvbuf(stdout, NULL, _IONBF, 0);
    WSAStartup(MAKEWORD(2, 2), &wd);

    printf("== how many spaces? ==\n");
    for (k = 0; k <= 4; ++k) {
        int i;
        for (i = 0; i < k; ++i) s[i] = ' ';
        s[k] = 0;
        { char show[32]; wsprintfA(show, "%d space(s)", k); one(s, show); }
    }

    printf("\n== a space, then something ==\n");
    one(" 1",      "\" 1\"");
    one(" 0",      "\" 0\"");
    one(" .",      "\" .\"");
    one(" x",      "\" x\"");
    one(" \t",     "\" \\t\"");
    one("\t ",     "\"\\t \"");
    one(" 1.2.3.4", "\" 1.2.3.4\"");

    printf("\n== each of the six whitespace bytes ALONE ==\n");
    {
        static const unsigned char WS[] = { 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x20 };
        unsigned i;
        for (i = 0; i < 6; ++i) {
            char show[32];
            s[0] = (char)WS[i]; s[1] = 0;
            wsprintfA(show, "the single byte %02X", WS[i]);
            one(s, show);
        }
    }

    printf("\n== and each of them alone AFTER a digit, which is the case that works ==\n");
    {
        static const unsigned char WS[] = { 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x20 };
        unsigned i;
        for (i = 0; i < 6; ++i) {
            char show[32];
            s[0] = '7'; s[1] = (char)WS[i]; s[2] = 0;
            wsprintfA(show, "\"7\" then %02X", WS[i]);
            one(s, show);
        }
    }

    printf("\n== is it the EMPTY part that is value 0, or the space itself? ==\n");
    one("",        "\"\" (empty)");
    one(".",       "\".\"");
    one(" .1",     "\" .1\"");
    one("1 .",     "\"1 .\"");
    one("1. ",     "\"1. \"");
    one("1.2.3. ", "\"1.2.3. \"");
    one("1.2.3.",  "\"1.2.3.\"");

    printf("\n== the same question with a NUL immediately after ==\n");
    printf("   (if \" \" is really \"an empty first part terminated by whitespace\", then a bare\n"
           "    empty string would be the same thing terminated by NUL -- and it is refused)\n");
    return 0;
}
