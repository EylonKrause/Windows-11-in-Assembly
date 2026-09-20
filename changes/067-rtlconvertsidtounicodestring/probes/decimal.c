/* changes/067-rtlconvertsidtounicodestring/probes/decimal.c
 *
 * This function is division-bound, and the constants that fix it are proved here, not quoted.
 *
 * impl.asm formats every number, the revision, the identifier authority and up to fifteen
 * sub-authorities, through a subroutine `du` whose inner loop is:
 *
 *     dul:    xor   edx, edx
 *             div   ecx              ; ecx = 10
 *             add   dl, 30h
 *             mov   byte ptr [r10], dl
 *             inc   r10
 *             test  eax, eax
 *             jnz   dul
 *
 * One 32-BIT division per decimal digit. a sub-authority like 2596069104 is ten digits, so it is
 * ten divisions, and `div` on this core has a latency in the twenties. The digits are then written
 * to a scratch byte buffer and read back in reverse, so every character is touched twice. The file
 * even declares `EXTERN wia_dec2b` (the two-digit table changes 054 and 202 use) and then never
 * references it.
 *
 * The replacement is the usual one: divide by 100 with a multiply and a shift, and emit two
 * characters at a time from a table. It rests on two claims that are ASSERTED everywhere they are
 * written down and PROVED nowhere:
 *
 *   1. (v * 0x51EB851F) >> 37, computed in 64 bits, equals v / 100 for every 32-bit v.
 *   2. digits10(v) = G[floor(log2(v|1))] + (v >= POW10[G[...]]) for a small table G.
 *
 * Both are checked against the compiler's own division over ALL 4294967296 values of v, because a
 * magic constant that is right for 99.9999% of its domain is a defect that no random corpus finds
 * and every production system eventually hits. That is the same standard change 267 applied to its
 * GF(2) shift tables, and the first draft of THOSE was wrong.
 *
 * It then times the three candidates so that the rewrite is justified by a number rather than by
 * the word "obviously".
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

/* ---------------------------------------------------------------- the two claims */

static const uint64_t POW10[11] = {
    1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull,
    100000000ull, 1000000000ull, 10000000000ull
};

/* G[b] = the number of decimal digits of the SMALLEST number with b+1 bits, i.e. of 2^b. */
static unsigned char G[32];

static void build_G(void)
{
    int b;
    for (b = 0; b < 32; ++b) {
        uint64_t v = 1ull << b;
        int n = 1;
        while (v >= POW10[n]) ++n;
        G[b] = (unsigned char)n;
    }
}

static unsigned bitlen(uint32_t v) { unsigned long i; _BitScanReverse(&i, v | 1u); return (unsigned)i; }

static unsigned digits10(uint32_t v)
{
    unsigned n = G[bitlen(v)];
    return n + (v >= POW10[n] ? 1u : 0u);      /* at most one correction */
}

/* ---------------------------------------------------------------- three converters */

static unsigned char dec2b[200];
static uint32_t      dec2w[100];               /* two UTF-16 characters packed into a dword */

static void build_tables(void)
{
    int i;
    for (i = 0; i < 100; ++i) {
        dec2b[2 * i]     = (unsigned char)('0' + i / 10);
        dec2b[2 * i + 1] = (unsigned char)('0' + i % 10);
        dec2w[i] = (uint32_t)('0' + i / 10) | ((uint32_t)('0' + i % 10) << 16);
    }
}

/* (a) what impl.asm does today: one division per digit, into a scratch, then reversed out */
static wchar_t* conv_div(wchar_t* d, uint32_t v)
{
    char scr[16];
    int k = 0;
    do { scr[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) *d++ = (wchar_t)scr[--k];
    return d;
}

/* (b) the same shape, but dividing by 100 with a multiply and emitting two at a time */
static wchar_t* conv_mul_scratch(wchar_t* d, uint32_t v)
{
    char scr[16];
    int k = 0;
    while (v >= 100u) {
        uint32_t q = (uint32_t)(((uint64_t)v * 0x51EB851Full) >> 37);
        uint32_t r = v - q * 100u;
        scr[k++] = (char)dec2b[2 * r + 1];
        scr[k++] = (char)dec2b[2 * r];
        v = q;
    }
    if (v >= 10u) { scr[k++] = (char)dec2b[2 * v + 1]; scr[k++] = (char)dec2b[2 * v]; }
    else           scr[k++] = (char)('0' + v);
    while (k) *d++ = (wchar_t)scr[--k];
    return d;
}

/* (c) what the rewrite does: the length FIRST, then pairs written backwards straight into the
       destination -- no scratch, no reversal, one dword store per two characters */
static wchar_t* conv_len_first(wchar_t* d, uint32_t v)
{
    unsigned n = digits10(v);
    wchar_t* e = d + n;
    wchar_t* p = e;
    while (n >= 2) {
        uint32_t q = (uint32_t)(((uint64_t)v * 0x51EB851Full) >> 37);
        uint32_t r = v - q * 100u;
        p -= 2;
        *(uint32_t*)p = dec2w[r];
        v = q;
        n -= 2;
    }
    if (n) *--p = (wchar_t)('0' + v);
    return e;
}

/* ---------------------------------------------------------------- timing */

static double freq;
static volatile uint64_t sink;

static double timeit(wchar_t* (*f)(wchar_t*, uint32_t), const uint32_t* vals, int nv)
{
    LARGE_INTEGER a, b;
    double best = 1e300;
    int pass;
    wchar_t buf[32];
    for (pass = 0; pass < 200; ++pass) {
        int i;
        uint64_t acc = 0;
        QueryPerformanceCounter(&a);
        for (i = 0; i < nv; ++i) acc += (uint64_t)(f(buf, vals[i]) - buf);
        QueryPerformanceCounter(&b);
        {
            double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / freq / nv;
            if (ns < best) best = ns;
        }
        sink += acc;
    }
    return best;
}

int main(void)
{
    LARGE_INTEGER f;
    uint64_t v;
    uint64_t bad_div = 0, bad_dig = 0;
    uint32_t first_bad_div = 0, first_bad_dig = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&f); freq = (double)f.QuadPart;
    build_G();
    build_tables();

    printf("== G, the digit count of 2^b, derived rather than typed ==\n  ");
    { int b; for (b = 0; b < 32; ++b) printf("%u ", G[b]); }
    printf("\n\n");

    printf("== claim 1: (v * 0x51EB851F) >> 37 == v / 100, for ALL 4294967296 values ==\n");
    printf("== claim 2: digits10(v) is exact,              for ALL 4294967296 values ==\n");
    printf("   (checked against the compiler's own division and its own printf)\n");
    for (v = 0; v <= 0xFFFFFFFFull; ++v) {
        uint32_t x = (uint32_t)v;
        uint32_t q = (uint32_t)(((uint64_t)x * 0x51EB851Full) >> 37);
        if (q != x / 100u) { if (!bad_div++) first_bad_div = x; }
        {
            unsigned n = digits10(x), m = 1;
            uint32_t t = x;
            while (t >= 10u) { t /= 10u; ++m; }
            if (n != m) { if (!bad_dig++) first_bad_dig = x; }
        }
        if ((x & 0x0FFFFFFFu) == 0x0FFFFFFFu) printf("   %3.0f%%\r", (double)v * 100.0 / 4294967296.0);
    }
    printf("   claim 1: %llu counterexample(s)%s\n", (unsigned long long)bad_div,
           bad_div ? "" : " -- EXACT over the whole 32-bit domain");
    if (bad_div) printf("            first at v = %u\n", first_bad_div);
    printf("   claim 2: %llu counterexample(s)%s\n", (unsigned long long)bad_dig,
           bad_dig ? "" : " -- EXACT over the whole 32-bit domain");
    if (bad_dig) printf("            first at v = %u\n", first_bad_dig);

    printf("\n== and they agree on the characters, over the same domain, spot-checked ==\n");
    {
        static const uint32_t SPOT[] = {
            0u, 1u, 9u, 10u, 11u, 99u, 100u, 101u, 999u, 1000u, 65535u, 999999999u,
            1000000000u, 2147483647u, 2596069104u, 4294967294u, 4294967295u
        };
        unsigned i;
        int bad = 0;
        for (i = 0; i < sizeof SPOT / sizeof SPOT[0]; ++i) {
            wchar_t a[32], b[32], c[32];
            wchar_t *ea = conv_div(a, SPOT[i]);
            wchar_t *eb = conv_mul_scratch(b, SPOT[i]);
            wchar_t *ec = conv_len_first(c, SPOT[i]);
            *ea = *eb = *ec = 0;
            printf("   %-12u  %-12ls %s\n", SPOT[i], a,
                   (lstrcmpW(a, b) == 0 && lstrcmpW(a, c) == 0) ? "" : "  <<< THEY DISAGREE");
            if (lstrcmpW(a, b) || lstrcmpW(a, c)) ++bad;
        }
        if (bad) printf("   %d disagreement(s)\n", bad);
    }

    printf("\n== what each one costs, per number ==\n");
    {
        static uint32_t big[4096], real[4096], small[4096];
        uint64_t rs = 0x243F6A8885A308D3ull;
        int i;
        for (i = 0; i < 4096; ++i) {
            rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
            big[i]   = (uint32_t)(rs >> 32);                 /* mostly 10 digits */
            real[i]  = 1000000000u + (uint32_t)(rs % 3000000000u) % 3294967295u;
            small[i] = (uint32_t)(rs % 1000u);               /* 1..3 digits */
        }
        printf("   %-34s %10s %10s %10s\n", "", "10-digit", "realistic", "1-3 digit");
        printf("   %-34s %9.2f  %9.2f  %9.2f\n", "a division per digit (impl.asm today)",
               timeit(conv_div, big, 4096), timeit(conv_div, real, 4096), timeit(conv_div, small, 4096));
        printf("   %-34s %9.2f  %9.2f  %9.2f\n", "multiply by 100, scratch + reverse",
               timeit(conv_mul_scratch, big, 4096), timeit(conv_mul_scratch, real, 4096),
               timeit(conv_mul_scratch, small, 4096));
        printf("   %-34s %9.2f  %9.2f  %9.2f\n", "length first, pairs written backwards",
               timeit(conv_len_first, big, 4096), timeit(conv_len_first, real, 4096),
               timeit(conv_len_first, small, 4096));
        printf("   (nanoseconds per number; lower is better)\n");
    }
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
