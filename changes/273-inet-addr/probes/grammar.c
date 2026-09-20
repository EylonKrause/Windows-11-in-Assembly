/* changes/273-inet-addr/probes/grammar.c
 *
 * What does ws2_32!inet_addr actually accept?
 *
 * discovery/sid_inet_bstr.c measured it at 24.98 ns and flagged it as the LENIENT IPv4 parser: it
 * takes "1.2", "0x7f.1" and octal, all of which RtlIpv4StringToAddressA -- change 114, already
 * converted -- refuses. So the two are NOT the same function and change 114 cannot simply be
 * wrapped, which is the first thing this file has to establish rather than assume.
 *
 * 24.98 ns is also small enough that the contract is most of the work. The classic inet_addr
 * grammar is four forms:
 *
 *     a.b.c.d     four bytes
 *     a.b.c       c is a 16-bit quantity in the low two bytes
 *     a.b         b is a 24-bit quantity in the low three bytes
 *     a           a is a 32-bit quantity
 *
 * with each part decimal, or octal with a leading 0, or hexadecimal with a leading 0x. That is what
 * the BSD implementation does and what every description of inet_addr says. Whether Microsoft's
 * agrees is a different question, and this project has been wrong about exactly that kind of
 * inherited description five times now -- change 269 found SIX places where one export disagreed
 * with its own documentation, and change 067's "the terminator needs two bytes" turned out to be
 * one byte.
 *
 * THE QUESTIONS, in the order they decide the implementation:
 *
 *   1. the FOUR FORMS -- are all four accepted, and is the packing the documented one?
 *   2. the BASES -- leading 0 for octal, 0x for hexadecimal, per part or carried?
 *   3. the OVERFLOW rules -- what happens when a part exceeds its field, and does it saturate,
 *      wrap, or refuse? This is where change 269 found a parser that SATURATES one field and
 *      REFUSES on another.
 *   4. Whitespace, signs and trailing text.
 *   5. what INADDR_NONE means, given that 255.255.255.255 is a legal address whose value IS
 *      INADDR_NONE -- so the failure signal is ambiguous by construction, and a caller cannot tell
 *      them apart. Does the export do anything about that?
 *   6. does it need WSAStartup, and what does it do with NULL?
 *
 * Nothing is asserted. Every line prints what the live export returned.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

static void one(const char* s)
{
    unsigned long v;
    WSASetLastError(0);
    v = inet_addr(s);
    printf("  %-28s -> %08lX", s ? s : "(null)", (unsigned long)v);
    if (v == INADDR_NONE) printf("   INADDR_NONE");
    else {
        unsigned char* b = (unsigned char*)&v;
        printf("   = %u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    }
    { int e = WSAGetLastError(); if (e) printf("   WSAGetLastError=%d", e); }
    printf("\n");
}

int main(void)
{
    WSADATA wd;
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== 0. does it work BEFORE WSAStartup? ==\n");
    one("127.0.0.1");
    printf("   (if that printed 7F000001 then no startup is needed, which a reimplementation\n"
           "    has to match: refusing before startup would be a behaviour of its own)\n");

    if (WSAStartup(MAKEWORD(2, 2), &wd)) { printf("WSAStartup failed\n"); return 1; }

    printf("\n== 1. the four forms ==\n");
    one("1.2.3.4");
    one("1.2.3");
    one("1.2");
    one("1");
    one("0");
    one("16909060");                 /* 0x01020304 as one part */
    one("1.131844");                 /* 0x020304 in three bytes */
    one("1.2.772");                  /* 0x0304 in two bytes */

    printf("\n== 2. the bases ==\n");
    one("0x1.0x2.0x3.0x4");
    one("0X1.0X2.0X3.0X4");
    one("010.010.010.010");          /* octal 8 */
    one("0.0.0.0");
    one("00.00.00.00");
    one("0x7f.1");
    one("0x7f000001");
    one("017700000001");             /* octal 0x7F000001 */
    one("0x");
    one("0x.1.2.3");
    one("08");                       /* not octal: 8 is not an octal digit */
    one("09.1.1.1");
    one("0xg");
    one("1.0x2.03.4");               /* mixed bases in one address */

    printf("\n== 3. overflow, per form ==\n");
    one("255.255.255.255");
    one("256.1.1.1");
    one("1.256.1.1");
    one("1.1.256.1");
    one("1.1.1.256");
    one("1.1.65535");
    one("1.1.65536");
    one("1.16777215");
    one("1.16777216");
    one("4294967295");
    one("4294967296");
    one("0xFFFFFFFF");
    one("0x100000000");
    one("0xFFFFFFFFFFFFFFFF");
    one("99999999999999999999");

    printf("\n== 4. whitespace, signs and trailing text ==\n");
    one(" 1.2.3.4");
    one("1.2.3.4 ");
    one("1.2.3.4\t");
    one("1. 2.3.4");
    one("+1.2.3.4");
    one("-1.2.3.4");
    one("1.2.3.4x");
    one("1.2.3.4.");
    one("1.2.3.4.5");
    one(".1.2.3");
    one("1..2.3");
    one("");
    one(".");
    one("....");
    one("1.2.3.4\n");

    printf("\n== 5. the ambiguity that is built into the contract ==\n");
    printf("   255.255.255.255 is a LEGAL address whose value IS INADDR_NONE, so a caller\n");
    printf("   cannot tell it from a refusal. Does the export distinguish them at all?\n");
    {
        unsigned long a, b;
        int ea, eb;
        WSASetLastError(0); a = inet_addr("255.255.255.255"); ea = WSAGetLastError();
        WSASetLastError(0); b = inet_addr("not-an-address");  eb = WSAGetLastError();
        printf("   \"255.255.255.255\" -> %08lX, WSAGetLastError=%d\n", (unsigned long)a, ea);
        printf("   \"not-an-address\"  -> %08lX, WSAGetLastError=%d\n", (unsigned long)b, eb);
        printf("   %s\n", (ea == eb) ? "   -- indistinguishable, as the documentation admits"
                                     : "   -- DISTINGUISHABLE through the last error");
    }

    printf("\n== 6. NULL, and a string with no terminator in reach ==\n");
    {
        unsigned long v;
        int faulted = 0;
        __try { v = inet_addr(0); }
        __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; faulted = 1; }
        if (faulted) printf("   inet_addr(NULL) FAULTED\n");
        else         printf("   inet_addr(NULL) -> %08lX\n", (unsigned long)v);
    }

    printf("\n== 7. and how it compares with RtlIpv4StringToAddressA, which change 114 owns ==\n");
    {
        typedef LONG (NTAPI *F_RTL)(const char*, BOOLEAN, const char**, void*);
        F_RTL f = (F_RTL)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlIpv4StringToAddressA");
        static const char* CASES[] = {
            "1.2.3.4", "1.2", "1", "0x7f.1", "010.1.1.1", "255.255.255.255", "256.1.1.1",
            " 1.2.3.4", "1.2.3.4 ", "1.2.3.4x"
        };
        unsigned i;
        if (!f) { printf("   RtlIpv4StringToAddressA not found\n"); }
        else for (i = 0; i < sizeof CASES / sizeof CASES[0]; ++i) {
            unsigned long addr = 0;
            const char* term = 0;
            LONG st = f(CASES[i], TRUE, &term, &addr);
            unsigned long iv = inet_addr(CASES[i]);
            printf("   %-20s inet_addr %08lX     Rtl %08lX (status %08lX)%s\n",
                   CASES[i], (unsigned long)iv, (unsigned long)addr, (unsigned long)st,
                   ((st >= 0) != (iv != INADDR_NONE)) ? "   <<< THEY DISAGREE" : "");
        }
    }
    return 0;
}
