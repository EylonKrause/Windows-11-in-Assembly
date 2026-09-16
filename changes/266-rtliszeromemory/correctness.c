/* changes/266-rtliszeromemory/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll!RtlIsZeroMemory.
 *
 * A PREDICATE HAS ONLY TWO ANSWERS, WHICH MAKES A CARELESS CORPUS VERY EASY TO PASS -- the lesson
 * change 259 wrote down for RtlAreBitsSet. An implementation that always answered "not zero" would
 * agree with the live export on nearly every random buffer, so every corpus here is built to
 * produce BOTH answers, and the harness COUNTS them and fails if either is missing.
 *
 *   1. EVERY LENGTH from 0 to 600 over an all-zero buffer, and the same with the LAST byte set --
 *      which is the position a scan is most likely to skip, and the one the overlapping final
 *      vector exists for.
 *   2. EVERY LENGTH from 0 to 300 with a single non-zero byte at EVERY position inside it. That is
 *      quadratic on purpose: a vector loop that mishandles one lane, or a tail that starts one
 *      byte late, shows up at exactly one (length, position) pair and nowhere else.
 *   3. A BYTE SET JUST PAST THE END at every length: it must not be seen.
 *   4. A GUARD PAGE, with the buffer ending EXACTLY at an inaccessible page, at every length --
 *      this is the corpus that proves the overlapping tail and the sub-32-byte ladder never read
 *      a byte the caller did not offer.
 *   5. EVERY BIT of a single byte, so "non-zero" does not quietly mean "0xFF".
 *   6. RANDOMISED, mostly-zero buffers at many lengths with one to a few bytes set.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOLEAN (NTAPI *F_IsZero)(const void*, SIZE_T);

BOOLEAN wia_iszeromemory(const void*, SIZE_T);

static F_IsZero live;
static long cases = 0, fails = 0, n_true = 0, n_false = 0;

/* the oracle: one byte at a time */
static BOOLEAN ref_iszeromemory(const void* p, SIZE_T n)
{
    const unsigned char* b = (const unsigned char*)p;
    SIZE_T i;
    for (i = 0; i < n; ++i) if (b[i]) return 0;
    return 1;
}

static void one(const void* p, SIZE_T n, const char* where)
{
    BOOLEAN ro, rr, rl;
    ++cases;
    ro = wia_iszeromemory(p, n);
    rr = ref_iszeromemory(p, n);
    rl = live(p, n);
    if (rl) ++n_true; else ++n_false;
    /* BOOLEAN is a byte: compare truthiness, not the byte, since a conforming implementation may
       return any non-zero value for TRUE */
    if (!!ro != !!rl || !!rr != !!rl) {
        if (++fails <= 20)
            printf("  MISMATCH [%s] len=%Iu: ours=%d ref=%d live=%d\n",
                   where, n, (int)ro, (int)rr, (int)rl);
    }
}

static unsigned long long rs = 0xBB67AE8584CAA73Bull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static unsigned char buf[4096];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    setvbuf(stdout, NULL, _IONBF, 0);
    live = (F_IsZero)GetProcAddress(h, "RtlIsZeroMemory");
    if (!live) { printf("resolve failed\n"); return 1; }

    printf("== CORRECTNESS: RtlIsZeroMemory ==\n");
    printf("   both answers are produced and counted; a corpus that only ever said one thing\n");
    printf("   would agree with an implementation that always said it\n");

    /* 1. every length, zero and with the last byte set */
    {
        long before = cases;
        int n;
        for (n = 0; n <= 600; ++n) {
            memset(buf, 0, 700);
            one(buf, (SIZE_T)n, "every length, all zero");
            if (n) {
                buf[n - 1] = 0x01;
                one(buf, (SIZE_T)n, "every length, LAST byte set");
            }
        }
        printf("  1. every length 0..600, all-zero and with the LAST byte set -- the position a\n"
               "     scan is most likely to skip: %ld\n", cases - before);
    }

    /* 2. a single non-zero byte at every position, at every length */
    {
        long before = cases;
        int n, pos;
        for (n = 1; n <= 300; ++n)
            for (pos = 0; pos < n; ++pos) {
                memset(buf, 0, 400);
                buf[pos] = 0x80;
                one(buf, (SIZE_T)n, "one byte set, every position");
            }
        printf("  2. ONE non-zero byte at EVERY position inside EVERY length 1..300 -- quadratic on\n"
               "     purpose, because a mishandled lane shows up at one pair and nowhere else: %ld\n",
               cases - before);
    }

    /* 3. a byte just past the end */
    {
        long before = cases;
        int n;
        for (n = 0; n <= 300; ++n) {
            memset(buf, 0, 400);
            buf[n] = 0xFF;                      /* one past the end of the range */
            one(buf, (SIZE_T)n, "a byte one past the end");
        }
        printf("  3. a byte set one past the end at every length -- it must not be seen: %ld\n",
               cases - before);
    }

    /* 4. a guard page */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  4. guard page SKIPPED\n");
        } else {
            int n, pos;
            for (n = 0; n <= 400; ++n) {
                unsigned char* p = (unsigned char*)(base + si.dwPageSize) - n;
                memset(p, 0, (size_t)n);
                one(p, (SIZE_T)n, "guard page, all zero");
                ++guard;
                if (n) {
                    p[n - 1] = 0x01;                        /* the very last readable byte */
                    one(p, (SIZE_T)n, "guard page, last byte set");
                    p[n - 1] = 0;
                    ++guard;
                    for (pos = 0; pos < n; pos += 7) {
                        p[pos] = 0x40;
                        one(p, (SIZE_T)n, "guard page, a byte set");
                        p[pos] = 0;
                        ++guard;
                    }
                }
            }
            printf("  4. the buffer ENDING at a PAGE_NOACCESS page, every length 0..400 and a byte\n"
                   "     set at every seventh position: %ld cases, no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* 5. every bit of a byte */
    {
        long before = cases;
        int b, n;
        for (b = 0; b < 8; ++b)
            for (n = 1; n <= 200; n += 7) {
                memset(buf, 0, 300);
                buf[n / 2] = (unsigned char)(1u << b);
                one(buf, (SIZE_T)n, "every bit of a byte");
            }
        printf("  5. each of the eight bits, so \"non-zero\" does not quietly mean 0xFF: %ld\n",
               cases - before);
    }

    /* 6. randomised */
    {
        long before = cases;
        int trial, k;
        for (trial = 0; trial < 200000; ++trial) {
            int n = (int)(rnd() % 3000);
            int howmany = (int)(rnd() % 4);           /* 0 means it stays all-zero */
            memset(buf, 0, 3100);
            for (k = 0; k < howmany && n; ++k) buf[rnd() % (unsigned)n] = (unsigned char)(1 + (rnd() % 255));
            one(buf, (SIZE_T)n, "randomised");
        }
        printf("  6. randomised lengths with zero to three bytes set: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf("  the live export answered TRUE %ld times and false %ld\n", n_true, n_false);
    if (!n_true || !n_false) { printf("CORRECTNESS: FAILED (only one answer was ever produced)\n"); return 1; }
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (exact vs the oracle AND the live export)\n");
    return fails ? 1 : 0;
}
