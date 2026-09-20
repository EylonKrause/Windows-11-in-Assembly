/* changes/267-rtlcrc32/shifttab.c
 *
 * The two "advance a crc past N zero bytes" tables, built once from the polynomial.
 *
 * Why they exist. probes/identify.c established that RtlCrc32 is CRC-32C, whose polynomial the
 * SSE4.2 CRC32 instruction implements in hardware, and that the shipped export runs a SERIAL
 * chain of that instruction. The instruction has 3-cycle latency and 1-per-cycle throughput, so a
 * serial chain runs at 8 bytes per 3 cycles, which is almost exactly the 14 GB/s the survey
 * measured. Three independent chains run at 8 bytes per cycle.
 *
 * Splitting the buffer into three is trivial; putting the three results back together is the part
 * that needs arithmetic. A CRC is linear over GF(2), so for three blocks of equal length L:
 *
 *      CRC(A||B||C) = shift_2L(crcA) ^ shift_L(crcB) ^ crcC
 *                   = shift_L( shift_L(crcA) ^ crcB ) ^ crcC
 *
 * where shift_L(c) is what c becomes after L more ZERO bytes go through the CRC. That is a fixed
 * GF(2) linear map on 32 bits, so it can be tabulated ONCE per L as four 256-entry tables indexed
 * by the four bytes of the CRC: four loads and three XORs, twice per 3L bytes of input.
 *
 * The tables are built from the polynomial at run time, not transcribed. a table pasted into this
 * repository would be a second copy of a constant nobody can check by reading it, and change 210
 * made the same choice for the upcase table for the same reason.
 *
 * How the operator is built. "One zero bit goes through the crc" is a 32x32 GF(2) matrix; squaring
 * it doubles the number of bits it advances, so the operator for any length is assembled by
 * square-and-multiply in about log2 steps. This is zlib's crc32_combine construction, written out
 * rather than lifted so that it can be read, and the first draft of this file muddled the
 * doubling and had to be rewritten, which is exactly why the tables are CHECKED below against the
 * definition they are supposed to satisfy.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define POLY_REFLECTED 0x82F63B78ul            /* CRC-32C, reflected */

/* both lengths must match the ones impl.asm uses */
#define WIA_CRC32_LONG   1024u
#define WIA_CRC32_SHORT    64u

unsigned long wia_crc32_long_shift[4][256];
unsigned long wia_crc32_short_shift[4][256];

/* A GF(2) linear map on 32 bits: column n is mat[n], and applying it to a vector is the XOR of the
   columns its set bits select. */
static unsigned long mat_times(const unsigned long* mat, unsigned long vec)
{
    unsigned long sum = 0;
    int i;
    for (i = 0; i < 32 && vec; ++i, vec >>= 1)
        if (vec & 1) sum ^= mat[i];
    return sum;
}

static void mat_square(unsigned long* square, const unsigned long* mat)
{
    int n;
    for (n = 0; n < 32; ++n) square[n] = mat_times(mat, mat[n]);
}

/* op := doubling_op composed with op */
static void mat_compose(unsigned long* op, const unsigned long* pre)
{
    unsigned long tmp[32];
    int n;
    for (n = 0; n < 32; ++n) tmp[n] = mat_times(pre, op[n]);
    for (n = 0; n < 32; ++n) op[n] = tmp[n];
}

static void build_operator(unsigned long* op, unsigned long bytes)
{
    unsigned long odd[32], even[32];
    unsigned long len = bytes;
    int n;

    odd[0] = POLY_REFLECTED;                   /* the operator for ONE zero bit */
    for (n = 1; n < 32; ++n) odd[n] = 1ul << (n - 1);

    mat_square(even, odd);                     /* two bits */
    mat_square(odd, even);                     /* four bits */

    for (n = 0; n < 32; ++n) op[n] = 1ul << n; /* start from the identity */

    /* `odd` is four bits here, so the first square below is EIGHT bits (one byte) and the loop
       consumes `len` in bytes from the bottom up, doubling as it goes. */
    for (;;) {
        mat_square(even, odd);                 /* one byte, then two, four, ... */
        if (len & 1) mat_compose(op, even);
        len >>= 1;
        if (!len) break;
        mat_square(odd, even);
        if (len & 1) mat_compose(op, odd);
        len >>= 1;
        if (!len) break;
    }
}

static void tabulate(unsigned long table[4][256], const unsigned long* op)
{
    unsigned b, i;
    for (b = 0; b < 4; ++b)
        for (i = 0; i < 256; ++i)
            table[b][i] = mat_times(op, ((unsigned long)i) << (b * 8));
}

/* The definition the tables must satisfy, computed the slow and obvious way: run `bytes` zero
   bytes through a bitwise CRC starting from `crc`. */
static unsigned long shift_by_definition(unsigned long crc, unsigned long bytes)
{
    unsigned long i;
    int k;
    for (i = 0; i < bytes; ++i)
        for (k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (POLY_REFLECTED & (unsigned long)(-(long)(crc & 1)));
    return crc;
}

static unsigned long apply(unsigned long table[4][256], unsigned long c)
{
    return table[0][c & 0xFF] ^ table[1][(c >> 8) & 0xFF] ^
           table[2][(c >> 16) & 0xFF] ^ table[3][(c >> 24) & 0xFF];
}

/* Returns 0 on success. The tables are checked against the definition rather than trusted: they
   encode the one piece of arithmetic in this change that cannot be seen to be right by reading it,
   and the first draft of the construction above was wrong. */
int wia_crc32_tables_init(void)
{
    unsigned long op[32];
    unsigned long probe[8] = { 0, 1, 0xFFFFFFFFul, 0x80000000ul, 0x12345678ul,
                               0xDEADBEEFul, 0xA5A5A5A5ul, 0x00010203ul };
    int i, bad = 0;

    build_operator(op, WIA_CRC32_LONG);
    tabulate(wia_crc32_long_shift, op);
    build_operator(op, WIA_CRC32_SHORT);
    tabulate(wia_crc32_short_shift, op);

    for (i = 0; i < 8; ++i) {
        if (apply(wia_crc32_long_shift, probe[i]) != shift_by_definition(probe[i], WIA_CRC32_LONG))
            ++bad;
        if (apply(wia_crc32_short_shift, probe[i]) != shift_by_definition(probe[i], WIA_CRC32_SHORT))
            ++bad;
    }
    return bad;
}
