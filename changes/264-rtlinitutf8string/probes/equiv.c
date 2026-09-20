/* changes/264-rtlinitutf8string/probes/equiv.c
 *
 * Is RtlInitUTF8String bit-for-bit RtlInitString on every input?
 *
 * probes/contract.c asked twenty-odd hand-picked strings and got the same answer from both exports
 * every time -- byte counting rather than character counting, NO UTF-8 validation of any kind, the
 * same 0xFFFF clamp (Length saturates at 65534 and MaximumLength at 65535), the same NULL rule.
 * Twenty strings chosen by the person who expects them to agree is not evidence, and this project
 * has paid four times over for a rule inherited from a sibling because it looked like the same rule
 * (the SPACE bug across changes 132, 140, 143 and 144).
 *
 * So this enumerates instead of sampling, over exactly the dimensions where a UTF-8 aware
 * implementation would have to differ:
 *
 *   1. Every single byte 0x01..0xFF as a one-byte string, and every ordered pair of bytes -- 65280
 *      two-byte strings, which contains every lead/continuation combination there is, every
 *      overlong prefix, and every truncated sequence.
 *   2. every LENGTH from 0 to 300, so a length-dependent rule cannot hide between the sizes a
 *      hand-written list happens to pick.
 *   3. Every length across the clamp, 65400..65700, where the two ushort fields saturate.
 *   4. RANDOM byte strings over alphabets that are pure ASCII, pure high-bit, valid UTF-8, and
 *      deliberately malformed UTF-8.
 *
 * All three fields are compared, not just Length: the struct is poisoned before each call, so a
 * field one export writes and the other does not is visible rather than invisible.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } U8STR;
typedef void (NTAPI *F_Init)(U8STR*, const char*);

static F_Init init_u8, init_str;
static long cases = 0, differ = 0;

static void cmp1(const char* s, const char* where)
{
    U8STR a, b;
    ++cases;
    memset(&a, 0xCD, sizeof a);
    memset(&b, 0xCD, sizeof b);
    init_u8(&a, s);
    init_str(&b, s);
    if (a.Length != b.Length || a.MaximumLength != b.MaximumLength || a.Buffer != b.Buffer) {
        if (++differ <= 20)
            printf("  DIFFER [%s] first bytes %02X %02X: UTF8{%u,%u,%p} STRING{%u,%u,%p}\n",
                   where, (unsigned char)s[0], (unsigned char)s[1],
                   a.Length, a.MaximumLength, (void*)a.Buffer,
                   b.Length, b.MaximumLength, (void*)b.Buffer);
    }
}

static unsigned long long rs = 0xC2B2AE3D27D4EB4Full;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    static char buf[70000];
    init_u8  = (F_Init)GetProcAddress(h, "RtlInitUTF8String");
    init_str = (F_Init)GetProcAddress(h, "RtlInitString");
    if (!init_u8 || !init_str) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== IS RtlInitUTF8String BIT-FOR-BIT RtlInitString? ==\n");
    printf("   all three fields compared, the struct poisoned with 0xCD before every call\n\n");

    /* 1. every byte, and every ordered pair */
    {
        long before = cases;
        unsigned i, j;
        char s[3];
        for (i = 1; i < 256; ++i) { s[0] = (char)i; s[1] = 0; cmp1(s, "every single byte"); }
        for (i = 1; i < 256; ++i)
            for (j = 1; j < 256; ++j) {
                s[0] = (char)i; s[1] = (char)j; s[2] = 0;
                cmp1(s, "every ordered byte pair");
            }
        printf("  1. every byte 0x01..0xFF and every ordered PAIR -- every lead/continuation\n"
               "     combination, every overlong prefix, every truncated sequence: %ld\n",
               cases - before);
    }

    /* 2. every length 0..300 */
    {
        long before = cases;
        int len, k;
        for (len = 0; len <= 300; ++len) {
            for (k = 0; k < len; ++k) buf[k] = (char)(0x21 + (k % 94));
            buf[len] = 0;
            cmp1(buf, "every length 0..300");
        }
        printf("  2. every length from 0 to 300: %ld\n", cases - before);
    }

    /* 3. every length across the clamp */
    {
        long before = cases;
        int len, k;
        for (len = 65400; len <= 65700; ++len) {
            for (k = 0; k < len; ++k) buf[k] = 'y';
            buf[len] = 0;
            cmp1(buf, "across the clamp");
        }
        printf("  3. every length from 65400 to 65700, across the USHORT clamp: %ld\n",
               cases - before);
    }

    /* 4. randomised over four alphabets */
    {
        long before = cases;
        int trial, k;
        for (trial = 0; trial < 60000; ++trial) {
            int mode = trial & 3, len = (int)(rnd() % 400);
            for (k = 0; k < len; ++k) {
                unsigned r = rnd();
                switch (mode) {
                case 0: buf[k] = (char)(0x20 + (r % 95)); break;             /* ASCII */
                case 1: buf[k] = (char)(0x80 | (r & 0x7F)); break;           /* high bit only */
                case 2:                                                      /* valid UTF-8 */
                    if ((r & 3) == 0 && k + 1 < len) { buf[k] = (char)0xC3; buf[++k] = (char)(0x80 | (r & 0x3F)); }
                    else buf[k] = (char)(0x41 + (r % 26));
                    break;
                default: buf[k] = (char)(r ? r : 1); break;                  /* anything but NUL */
                }
            }
            buf[len] = 0;
            cmp1(buf, "randomised");
        }
        printf("  4. randomised: ASCII, high-bit, valid UTF-8 and deliberately malformed: %ld\n",
               cases - before);
    }

    /* and NULL */
    cmp1(NULL, "NULL");

    printf("\n  total: %ld,  differences: %ld\n", cases, differ);
    if (differ) {
        printf("\nTHEY ARE NOT THE SAME FUNCTION -- change 095's implementation cannot be reused\n");
        return 1;
    }
    printf("\nIDENTICAL on every input above. RtlInitUTF8String is RtlInitString at a second\n"
           "address: it counts BYTES, validates NOTHING, and clamps the same way.\n");
    return 0;
}
