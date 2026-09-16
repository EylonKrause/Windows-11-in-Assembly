/* changes/267-rtlcrc32/probes/identify.c
 *
 * WHICH CRC-32 IS ntdll!RtlCrc32?
 *
 * discovery/ntdll_bitmap3.c measured it at 4627.90 ns for 64 KB -- 0.071 ns/byte, the worst
 * per-byte cost found anywhere in ntdll this session. Nothing can be written until the exact
 * variant is known, and "it is probably zlib" is not knowing.
 *
 * Change 076 did this for RtlCrc64 and it did not come out where anyone would have guessed: a
 * reflected CRC-64 with polynomial 0x9A6C9329AC4BC9B5, an internal accumulator of ~Init and an
 * output of ~crc. So the parameters are DERIVED here rather than matched against a table of
 * popular ones:
 *
 *   1. THE STANDARD TEST VECTOR "123456789" against every common CRC-32 variant. If one matches,
 *      that is a hypothesis and not yet a conclusion.
 *   2. THE POLYNOMIAL, read out of the function directly: feed a single 1 bit and see which bits
 *      come back. For a reflected CRC with a zero accumulator, one byte 0x01 produces the
 *      polynomial's own bit pattern after eight shifts, which pins it without guessing.
 *   3. THE INITIAL VALUE, by asking for the CRC of an EMPTY buffer: whatever comes back is the
 *      init run through the output transform, with no message to confuse it.
 *   4. THE OUTPUT TRANSFORM, by checking whether CRC(init=X) of an empty buffer is X or ~X.
 *   5. CHAINING: is the third argument really a running CRC? If CRC(b, n2, CRC(a, n1, 0)) equals
 *      CRC(ab, n1+n2, 0) then it is, and an implementation may process a buffer in pieces.
 *   6. And the hypothesis is then CONFIRMED OR REJECTED over a few thousand random buffers
 *      against a from-scratch bitwise implementation of the derived parameters -- which is the
 *      only step that actually proves anything.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef ULONG (NTAPI *F_Crc32)(const void*, SIZE_T, ULONG);
static F_Crc32 crc32;

/* a from-scratch REFLECTED crc, one bit at a time, with every parameter explicit */
static ULONG bitwise_reflected(const unsigned char* p, SIZE_T n, ULONG poly, ULONG init,
                               ULONG xorout)
{
    ULONG c = init;
    SIZE_T i;
    int k;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (k = 0; k < 8; ++k) c = (c >> 1) ^ (poly & (ULONG)(-(LONG)(c & 1)));
    }
    return c ^ xorout;
}

/* ... and a NON-reflected one, in case it is not reflected at all */
static ULONG bitwise_normal(const unsigned char* p, SIZE_T n, ULONG poly, ULONG init, ULONG xorout)
{
    ULONG c = init;
    SIZE_T i;
    int k;
    for (i = 0; i < n; ++i) {
        c ^= ((ULONG)p[i]) << 24;
        for (k = 0; k < 8; ++k) c = (c << 1) ^ (poly & (ULONG)(-(LONG)((c >> 31) & 1)));
    }
    return c ^ xorout;
}

static unsigned long long rs = 0x510E527FADE682D1ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    static const unsigned char CHECK[9] = { '1','2','3','4','5','6','7','8','9' };
    static unsigned char buf[8192];
    ULONG live_check;
    crc32 = (F_Crc32)GetProcAddress(h, "RtlCrc32");
    if (!crc32) { printf("RtlCrc32 not exported\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== WHICH CRC-32 IS RtlCrc32? ==\n\n");

    printf("-- 1. the standard check value, \"123456789\" --\n");
    live_check = crc32(CHECK, 9, 0);
    printf("   RtlCrc32(\"123456789\", 9, 0)          = %08lX\n", (unsigned long)live_check);
    printf("   CRC-32 (zlib, refl 0xEDB88320)        = %08lX\n",
           (unsigned long)bitwise_reflected(CHECK, 9, 0xEDB88320ul, 0xFFFFFFFFul, 0xFFFFFFFFul));
    printf("   CRC-32C (refl 0x82F63B78)             = %08lX\n",
           (unsigned long)bitwise_reflected(CHECK, 9, 0x82F63B78ul, 0xFFFFFFFFul, 0xFFFFFFFFul));
    printf("   CRC-32K (refl 0xEB31D82E)             = %08lX\n",
           (unsigned long)bitwise_reflected(CHECK, 9, 0xEB31D82Eul, 0xFFFFFFFFul, 0xFFFFFFFFul));
    printf("   CRC-32/BZIP2 (normal 0x04C11DB7)      = %08lX\n",
           (unsigned long)bitwise_normal(CHECK, 9, 0x04C11DB7ul, 0xFFFFFFFFul, 0xFFFFFFFFul));
    printf("   CRC-32/MPEG-2 (normal, no xorout)     = %08lX\n",
           (unsigned long)bitwise_normal(CHECK, 9, 0x04C11DB7ul, 0xFFFFFFFFul, 0ul));
    printf("   CRC-32 with init 0 and no xorout      = %08lX\n",
           (unsigned long)bitwise_reflected(CHECK, 9, 0xEDB88320ul, 0ul, 0ul));

    printf("\n-- 2. THE POLYNOMIAL, read straight out of the function --\n");
    printf("   A reflected CRC of the single byte 0x01, with a zero accumulator, is the polynomial\n");
    printf("   itself shifted -- so this does not need a table of candidates to compare against.\n");
    {
        unsigned char one = 0x01;
        ULONG r = crc32(&one, 1, 0);
        printf("   RtlCrc32({0x01}, 1, 0) = %08lX\n", (unsigned long)r);
        printf("   poly 0xEDB88320 (zlib), init 0, xorout 0            = %08lX\n",
               (unsigned long)bitwise_reflected(&one, 1, 0xEDB88320ul, 0ul, 0ul));
        printf("   poly 0x82F63B78 (Castagnoli), init ~0, then ~result = %08lX\n",
               (unsigned long)~bitwise_reflected(&one, 1, 0x82F63B78ul, 0xFFFFFFFFul, 0ul));
        printf("   -- so a single byte does NOT identify the polynomial on its own once the\n"
               "      accumulator is complemented at both ends, which is why section 6 exists:\n"
               "      the reading in this section's own heading was too optimistic.\n");
    }

    printf("\n-- 3. THE INITIAL VALUE and 4. THE OUTPUT TRANSFORM --\n");
    {
        ULONG e0 = crc32(buf, 0, 0);
        ULONG eF = crc32(buf, 0, 0xFFFFFFFFul);
        ULONG e5 = crc32(buf, 0, 0x12345678ul);
        printf("   an EMPTY buffer with the third argument 0          -> %08lX\n", (unsigned long)e0);
        printf("   ... with 0xFFFFFFFF                                -> %08lX\n", (unsigned long)eF);
        printf("   ... with 0x12345678                                -> %08lX\n", (unsigned long)e5);
        printf("   %s\n", (e0 == 0 && e5 == 0x12345678ul)
               ? "the third argument passes straight through: it IS the running CRC, with no\n"
                 "   inversion on the way in or out"
               : "the third argument is transformed on the way through -- see the values above");
    }

    printf("\n-- 5. CHAINING: is the third argument a running CRC? --\n");
    {
        int i, bad = 0;
        for (i = 0; i < 200; ++i) {
            ULONG n1 = 1 + (rnd() % 100), n2 = 1 + (rnd() % 100), whole, piece;
            ULONG k;
            for (k = 0; k < n1 + n2; ++k) buf[k] = (unsigned char)rnd();
            whole = crc32(buf, n1 + n2, 0);
            piece = crc32(buf + n1, n2, crc32(buf, n1, 0));
            if (whole != piece) ++bad;
        }
        printf("   200 random splits: %d disagree -- %s\n", bad,
               bad ? "it is NOT a plain running CRC"
                   : "chaining works, so a buffer may be processed in pieces");
    }

    printf("\n-- 6. THE HYPOTHESIS, CONFIRMED OR REJECTED over random buffers --\n");
    {
        int trial, bad = 0;
        for (trial = 0; trial < 5000; ++trial) {
            ULONG n = rnd() % 4000, init = rnd(), k;
            ULONG live, mine;
            for (k = 0; k < n; ++k) buf[k] = (unsigned char)rnd();
            live = crc32(buf, n, init);
            /* THE HYPOTHESIS, after the three measurements above disagreed with the obvious one.
               The check value is CRC-32C's, so the polynomial is Castagnoli's 0x82F63B78 -- but
               an EMPTY buffer returns the third argument UNCHANGED, which a plain init/xorout of
               0xFFFFFFFF cannot do. Both are true at once if the function complements on the way
               IN and again on the way OUT, which is exactly the shape change 076 found for
               RtlCrc64: internal accumulator = ~Init, output = ~crc. */
            mine = ~bitwise_reflected(buf, n, 0x82F63B78ul, ~init, 0ul);
            if (live != mine) {
                if (++bad <= 5)
                    printf("   DIFFER at length %lu init %08lX: live=%08lX mine=%08lX\n",
                           (unsigned long)n, (unsigned long)init,
                           (unsigned long)live, (unsigned long)mine);
            }
        }
        printf("   5000 random buffers and initial values against\n"
               "       ~CRC(poly 0x82F63B78, init = ~argument, xorout 0)\n"
               "   : %d disagreements\n", bad);
        printf("%s\n", bad ? "   THE HYPOTHESIS IS WRONG -- do not build on it"
                           : "   CONFIRMED: RtlCrc32 is CRC-32C -- Castagnoli, reflected polynomial\n"
                             "   0x82F63B78 -- with the accumulator complemented on the way IN and\n"
                             "   again on the way OUT. That is why the check value is CRC-32C's\n"
                             "   E3069283 while an EMPTY buffer still returns the argument\n"
                             "   unchanged: the two complements cancel when there is no message.\n"
                             "   It is the same shape change 076 found for RtlCrc64.\n"
                             "\n"
                             "   AND 0x82F63B78 IS THE POLYNOMIAL THE SSE4.2 CRC32 INSTRUCTION\n"
                             "   IMPLEMENTS IN HARDWARE, which is what makes this worth doing: the\n"
                             "   instruction is 3-cycle latency and 1-per-cycle throughput, so a\n"
                             "   serial chain of them runs at 8 bytes per 3 cycles -- almost\n"
                             "   exactly the 14 GB/s the shipped export measures -- and splitting\n"
                             "   the buffer into independent streams is the whole optimisation.");
    }
    return 0;
}
