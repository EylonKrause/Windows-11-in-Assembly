/* changes/279-rtlintegertochar/probes/contract.c
 *
 * The other two formatters: RtlIntegerToChar and RtlLargeIntegerToChar.
 *
 * discovery/rtl_integer_char.c measured them at 13.62 ns for ten decimal digits and 27.75 ns for
 * nineteen, against change 278's 4.28 ns for the same ten. They are the ANSI and 64-bit siblings of
 * the export change 278 just replaced, and the interesting one is the 64-bit form: change 278's
 * whole method rests on a divide-by-100 that is proved exact only over the 32-bit domain, so the
 * upper half of a LARGE_INTEGER cannot use it.
 *
 * But the signatures are not the same shape, and that is the first thing to establish:
 *
 *     NTSTATUS RtlIntegerToChar(ULONG value, ulong base, long length, psz string)
 *     NTSTATUS RtlLargeIntegerToChar(PLARGE_INTEGER value, ulong base, long length, psz string)
 *
 * a length and a raw pointer, not a UNICODE_STRING, so there is no Length field to set and the
 * room rule is whatever `length` means. It could be "bytes available" or "exactly this many
 * characters, padded". The documentation for the pair is thin enough that guessing would be
 * reckless, and change 278's room rule turned out to differ from change 067's by one byte in the
 * same DLL.
 *
 * THE QUESTIONS:
 *
 *   1. WHAT IS `length`? Room, or a fixed field width? What does 0 mean? What does a negative mean?
 *   2. Is the result terminated, and is the terminator counted against `length`?
 *   3. THE BASES: the same five as change 278, or a different set?
 *   4. The room rule, swept one byte at a time, because that is where the last two changes differed.
 *   5. ON FAILURE, what is left in the buffer?
 *   6. And for the 64-bit form: is it signed? `LARGE_INTEGER` is, but change 278's ULONG was not.
 *
 * Nothing is asserted. Every line prints what the live exports returned.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG (NTAPI *F_I2C)(ULONG, ULONG, LONG, char*);
typedef LONG (NTAPI *F_LI2C)(LARGE_INTEGER*, ULONG, LONG, char*);

static F_I2C  i2c;
static F_LI2C li2c;
static char buf[256];

#define POISON '#'

static void show(const char* what, LONG st, int room)
{
    int i;
    printf("  %-34s -> %08lX  \"", what, (unsigned long)st);
    for (i = 0; i < room && i < 40 && buf[i] && buf[i] != POISON; ++i) putchar(buf[i]);
    printf("\"  bytes:");
    for (i = 0; i < 12 && i < room + 2; ++i) printf(" %02X", (unsigned char)buf[i]);
    printf("\n");
}

static void one(const char* what, ULONG v, ULONG base, LONG len)
{
    memset(buf, POISON, sizeof buf);
    show(what, i2c(v, base, len, buf), len > 0 ? len : 0);
}

static void onel(const char* what, long long v, ULONG base, LONG len)
{
    LARGE_INTEGER q;
    q.QuadPart = v;
    memset(buf, POISON, sizeof buf);
    show(what, li2c(&q, base, len, buf), len > 0 ? len : 0);
}

int main(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    ULONG b;
    int k;

    setvbuf(stdout, NULL, _IONBF, 0);
    i2c  = (F_I2C) GetProcAddress(hn, "RtlIntegerToChar");
    li2c = (F_LI2C)GetProcAddress(hn, "RtlLargeIntegerToChar");
    if (!i2c || !li2c) { printf("resolve failed\n"); return 1; }

    printf("== 1. WHAT IS `length`? \"3735928559\" is ten characters ==\n");
    for (k = 0; k <= 14; ++k) {
        char nm[48];
        wsprintfA(nm, "RtlIntegerToChar, length %d", k);
        one(nm, 3735928559ul, 10, k);
    }
    printf("   (if length 10 succeeds the terminator is NOT counted; if only 11 does, it is)\n");

    printf("\n== 2. a negative length, which the parameter's type allows ==\n");
    one("length -1", 3735928559ul, 10, -1);
    one("length -100", 3735928559ul, 10, -100);
    one("length 0", 3735928559ul, 10, 0);

    printf("\n== 3. is a SHORT number padded to `length`, or written at the front? ==\n");
    one("value 7, length 1",  7, 10, 1);
    one("value 7, length 2",  7, 10, 2);
    one("value 7, length 8",  7, 10, 8);
    one("value 42, length 8", 42, 10, 8);
    printf("   (leading zeros or spaces in the bytes above mean it is a FIELD WIDTH, not room)\n");

    printf("\n== 4. the bases -- the same five as change 278? ==\n");
    for (b = 0; b <= 20; ++b) {
        char nm[48];
        wsprintfA(nm, "base %lu", (unsigned long)b);
        one(nm, 3735928559ul, b, 40);
    }
    one("base 36", 3735928559ul, 36, 40);
    one("base 0xFFFFFFFF", 3735928559ul, 0xFFFFFFFFul, 40);

    printf("\n== 5. what does a REFUSAL leave in the buffer? ==\n");
    {
        memset(buf, POISON, sizeof buf);
        printf("   too short:  %08lX, first bytes %02X %02X %02X %02X\n",
               (unsigned long)i2c(3735928559ul, 10, 3, buf),
               (unsigned char)buf[0], (unsigned char)buf[1],
               (unsigned char)buf[2], (unsigned char)buf[3]);
        memset(buf, POISON, sizeof buf);
        printf("   bad base:   %08lX, first bytes %02X %02X %02X %02X\n",
               (unsigned long)i2c(3735928559ul, 7, 40, buf),
               (unsigned char)buf[0], (unsigned char)buf[1],
               (unsigned char)buf[2], (unsigned char)buf[3]);
        memset(buf, POISON, sizeof buf);
        printf("   length 0:   %08lX, first bytes %02X %02X\n",
               (unsigned long)i2c(3735928559ul, 10, 0, buf),
               (unsigned char)buf[0], (unsigned char)buf[1]);
    }

    printf("\n== 6. RtlLargeIntegerToChar: is it SIGNED? ==\n");
    onel("1234567890123456789, base 10", 1234567890123456789ll, 10, 40);
    onel("-1, base 10",                  -1ll, 10, 40);
    onel("-1, base 16",                  -1ll, 16, 40);
    onel("0x8000000000000000, base 10",  (long long)0x8000000000000000ull, 10, 40);
    onel("0, base 10",                   0ll, 10, 40);
    onel("0xFFFFFFFFFFFFFFFF, base 16",  -1ll, 16, 40);
    onel("4294967296, base 10",          4294967296ll, 10, 40);
    onel("4294967295, base 10",          4294967295ll, 10, 40);

    printf("\n== 7. the 64-bit room rule, swept ==\n");
    for (k = 17; k <= 23; ++k) {
        char nm[48];
        wsprintfA(nm, "19 digits, length %d", k);
        onel(nm, 1234567890123456789ll, 10, k);
    }

    printf("\n== 8. and its bases ==\n");
    for (b = 0; b <= 17; ++b) {
        char nm[48];
        wsprintfA(nm, "large, base %lu", (unsigned long)b);
        onel(nm, 1234567890123456789ll, b, 80);
    }

    printf("\n== 9. the longest answer in each base, 64-bit ==\n");
    onel("all ones, base 2",  -1ll, 2, 80);
    onel("all ones, base 8",  -1ll, 8, 80);
    onel("all ones, base 10", -1ll, 10, 80);
    onel("all ones, base 16", -1ll, 16, 80);
    return 0;
}
