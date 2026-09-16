/* changes/278-rtlintegertounicodestring/probes/contract.c
 *
 * WHAT DOES RtlIntegerToUnicodeString ACTUALLY DO, AND IS IT THE SAME SHAPE CHANGE 067 FIXED?
 *
 * discovery/rtl_integer_char.c measured it at 32.14 ns for ten decimal digits, 18.66 for eight
 * hexadecimal, 20.35 for eleven octal, 40.27 for thirty-two binary -- and 10.16 ns for a SINGLE
 * DIGIT. Change 067's rewritten formatter does a whole 32-bit number in 3.76 ns, and the
 * division-per-digit version it replaced took 12.77.
 *
 * The per-base spread is the tell. 32 digits of binary costing only twice what 10 digits of decimal
 * costs, while one digit still costs 10 ns, says the cost is mostly FIXED and partly per-digit --
 * which is what a division loop plus a validation prologue looks like.
 *
 * THIS FAMILY IS WORTH GETTING RIGHT because it is four exports that share a core:
 * RtlIntegerToUnicodeString, RtlIntegerToChar, RtlLargeIntegerToChar, and (inverted)
 * RtlUnicodeStringToInteger. This file is about the first, and about what all of them have in
 * common.
 *
 * THE QUESTIONS, in the order they decide the implementation:
 *
 *   1. WHICH BASES are accepted, and what does an unsupported one do? The documented set is 0, 2,
 *      8, 10, 16 -- and 0 is documented to mean 10. What about 3, 7, 36?
 *   2. IS THERE A PREFIX? RtlUnicodeStringToInteger reads "0x"; does the formatter write one?
 *   3. WHAT IS THE ROOM RULE? It writes into a UNICODE_STRING with a MaximumLength, and change 067
 *      found that the analogous rule was Length+1 and not Length+2 -- a whole byte away from what
 *      everyone writes down.
 *   4. IS Length SET, IS THE BUFFER TERMINATED, and what happens to the destination on failure?
 *   5. SIGNEDNESS. The argument is a ULONG. Is 0xFFFFFFFF "4294967295" or "-1"?
 *   6. And the digits above 9: upper case or lower?
 *
 * Nothing is asserted. Every line prints what the live export returned.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef LONG (NTAPI *F_I2US)(ULONG, ULONG, USTR*);

static F_I2US i2us;
static wchar_t buf[512];

#define POISON 0x2A2A

static void one(const char* what, ULONG v, ULONG base, USHORT maxlen)
{
    USTR u;
    LONG st;
    int i;
    for (i = 0; i < 512; ++i) buf[i] = POISON;
    u.Length = 0xBEEF;
    u.MaximumLength = maxlen;
    u.Buffer = buf;
    st = i2us(v, base, &u);
    printf("  %-28s v=%-11lu base=%-3lu max=%-4u -> %08lX  Length=%-6u ",
           what, (unsigned long)v, (unsigned long)base, maxlen, (unsigned long)st, u.Length);
    if (st >= 0) {
        printf("\"");
        for (i = 0; i < u.Length / 2 && i < 40; ++i) printf("%lc", buf[i]);
        printf("\"");
        printf("  [%d]=%04X", u.Length / 2, buf[u.Length / 2]);
    } else {
        printf("(refused)  buffer[0]=%04X", buf[0]);
    }
    printf("\n");
}

int main(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    ULONG b;

    setvbuf(stdout, NULL, _IONBF, 0);
    i2us = (F_I2US)GetProcAddress(hn, "RtlIntegerToUnicodeString");
    if (!i2us) { printf("resolve failed\n"); return 1; }

    printf("== 1. the documented bases, and the ones that are not ==\n");
    for (b = 0; b <= 20; ++b) {
        char nm[32];
        wsprintfA(nm, "base %lu", (unsigned long)b);
        one(nm, 3735928559ul, b, 200);
    }
    one("base 36", 3735928559ul, 36, 200);
    one("base 256", 3735928559ul, 256, 200);
    one("base 0xFFFFFFFF", 3735928559ul, 0xFFFFFFFFul, 200);

    printf("\n== 2. is there a prefix, and what case are the digits? ==\n");
    one("hex, letters",   0xABCDEFul, 16, 200);
    one("hex, one digit", 0xFul, 16, 200);
    one("binary",         5, 2, 200);
    one("octal",          8, 8, 200);
    one("decimal zero",   0, 10, 200);
    one("hex zero",       0, 16, 200);
    one("binary zero",    0, 2, 200);
    one("octal zero",     0, 8, 200);

    printf("\n== 3. THE ROOM RULE. Change 067 found the analogous one was Length+1, not Length+2 ==\n");
    {
        /* find the exact length first */
        USTR u;
        int i;
        USHORT need;
        for (i = 0; i < 512; ++i) buf[i] = POISON;
        u.Length = 0; u.MaximumLength = 500; u.Buffer = buf;
        i2us(3735928559ul, 10, &u);
        need = u.Length;
        printf("   \"3735928559\" is %u bytes (%u characters)\n", need, need / 2);
        for (i = (int)need - 2; i <= (int)need + 3; ++i) {
            char nm[32];
            if (i < 0) continue;
            wsprintfA(nm, "max = Length%+d", i - (int)need);
            one(nm, 3735928559ul, 10, (USHORT)i);
        }
    }

    printf("\n== 4. what does a REFUSAL leave behind? ==\n");
    {
        USTR u;
        int i;
        for (i = 0; i < 512; ++i) buf[i] = POISON;
        u.Length = 0xBEEF; u.MaximumLength = 4; u.Buffer = buf;
        printf("   too small: status %08lX, Length now %u, buffer %04X %04X %04X\n",
               (unsigned long)i2us(3735928559ul, 10, &u), u.Length, buf[0], buf[1], buf[2]);
        for (i = 0; i < 512; ++i) buf[i] = POISON;
        u.Length = 0xBEEF; u.MaximumLength = 0; u.Buffer = buf;
        printf("   max 0:     status %08lX, Length now %u, buffer %04X\n",
               (unsigned long)i2us(3735928559ul, 10, &u), u.Length, buf[0]);
        for (i = 0; i < 512; ++i) buf[i] = POISON;
        u.Length = 0xBEEF; u.MaximumLength = 200; u.Buffer = buf;
        printf("   bad base:  status %08lX, Length now %u, buffer %04X\n",
               (unsigned long)i2us(3735928559ul, 7, &u), u.Length, buf[0]);
    }

    printf("\n== 5. signedness: the argument is a ULONG ==\n");
    one("0xFFFFFFFF, base 10", 0xFFFFFFFFul, 10, 200);
    one("0x80000000, base 10", 0x80000000ul, 10, 200);
    one("0xFFFFFFFF, base 16", 0xFFFFFFFFul, 16, 200);
    one("0xFFFFFFFF, base 2",  0xFFFFFFFFul, 2, 200);
    one("0xFFFFFFFF, base 8",  0xFFFFFFFFul, 8, 200);

    printf("\n== 6. is the buffer terminated, and is the terminator inside MaximumLength? ==\n");
    {
        USTR u;
        int i;
        for (i = 0; i < 512; ++i) buf[i] = POISON;
        u.Length = 0; u.MaximumLength = 200; u.Buffer = buf;
        i2us(12345, 10, &u);
        printf("   ample room:   Length %u, then %04X %04X %04X\n",
               u.Length, buf[u.Length / 2], buf[u.Length / 2 + 1], buf[u.Length / 2 + 2]);
        for (i = 0; i < 512; ++i) buf[i] = POISON;
        u.Length = 0; u.MaximumLength = 10; u.Buffer = buf;
        i2us(12345, 10, &u);
        printf("   exactly 10:   Length %u, then %04X %04X\n",
               u.Length, buf[u.Length / 2], buf[u.Length / 2 + 1]);
        for (i = 0; i < 512; ++i) buf[i] = POISON;
        u.Length = 0; u.MaximumLength = 12; u.Buffer = buf;
        i2us(12345, 10, &u);
        printf("   exactly 12:   Length %u, then %04X %04X\n",
               u.Length, buf[u.Length / 2], buf[u.Length / 2 + 1]);
    }

    printf("\n== 7. every value 0..66000 and a sweep of each base, for the length rule ==\n");
    {
        static const ULONG BASES[] = { 2, 8, 10, 16 };
        unsigned bi;
        for (bi = 0; bi < 4; ++bi) {
            ULONG v;
            int minlen = 999, maxlen = 0;
            for (v = 0; v < 66000; ++v) {
                USTR u;
                u.Length = 0; u.MaximumLength = 500; u.Buffer = buf;
                if (i2us(v, BASES[bi], &u) >= 0) {
                    if (u.Length < minlen) minlen = u.Length;
                    if (u.Length > maxlen) maxlen = u.Length;
                }
            }
            printf("   base %2lu: lengths %d..%d bytes over 0..65999\n",
                   (unsigned long)BASES[bi], minlen, maxlen);
        }
    }
    return 0;
}
