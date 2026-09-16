/* changes/267-rtlcrc32/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll!RtlCrc32.
 *
 * THE ORACLE IS BITWISE AND SHARES NOTHING WITH THE IMPLEMENTATION. impl.asm uses the SSE4.2 CRC32
 * instruction in three parallel chains and recombines them through a linear-algebra table; the
 * oracle shifts one bit at a time through the polynomial probes/identify.c derived. If the table
 * construction were wrong, the two would part company immediately -- which is the point.
 *
 * A CRC IS A 32-BIT VALUE, SO A WRONG IMPLEMENTATION IS OVERWHELMINGLY LIKELY TO BE OBVIOUSLY
 * WRONG, but the ways it can be subtly wrong are all about BOUNDARIES, and that is where the
 * corpora go:
 *
 *   1. EVERY LENGTH from 0 to 4000. That crosses 192 (where the short blocks begin), 3072 (where
 *      the long blocks begin), and every remainder either leaves behind -- the 8/4/2/1 tail is
 *      exercised at every possible residue, not at a few chosen ones.
 *   2. EVERY LENGTH again with a NON-ZERO initial CRC, because the initial value only enters the
 *      FIRST of the three chains and an implementation that seeded the wrong chain, or seeded all
 *      three, would pass every test that started from zero.
 *   3. CHAINING: the CRC of a buffer computed in one call must equal the same buffer computed in
 *      two calls with the first result fed to the second, at splits either side of every block
 *      boundary.
 *   4. A GUARD PAGE: the buffer ending exactly at an inaccessible page at every length.
 *   5. RANDOMISED lengths, contents and initial values.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef ULONG (NTAPI *F_Crc32)(const void*, SIZE_T, ULONG);

ULONG wia_crc32(const void*, SIZE_T, ULONG);
int wia_crc32_tables_init(void);

static F_Crc32 live;
static long cases = 0, fails = 0;

/* the oracle: the parameters probes/identify.c derived, one bit at a time */
#define POLY 0x82F63B78ul
static ULONG ref_crc32(const void* p, SIZE_T n, ULONG init)
{
    const unsigned char* b = (const unsigned char*)p;
    ULONG c = ~init;
    SIZE_T i;
    int k;
    for (i = 0; i < n; ++i) {
        c ^= b[i];
        for (k = 0; k < 8; ++k) c = (c >> 1) ^ (POLY & (ULONG)(-(LONG)(c & 1)));
    }
    return ~c;
}

static void one(const void* p, SIZE_T n, ULONG init, const char* where)
{
    ULONG ro, rr, rl;
    ++cases;
    ro = wia_crc32(p, n, init);
    rr = ref_crc32(p, n, init);
    rl = live(p, n, init);
    if (ro != rl || rr != rl) {
        if (++fails <= 20)
            printf("  MISMATCH [%s] len=%Iu init=%08lX: ours=%08lX ref=%08lX live=%08lX\n",
                   where, n, (unsigned long)init, (unsigned long)ro, (unsigned long)rr,
                   (unsigned long)rl);
    }
}

static unsigned long long rs = 0x9B05688C2B3E6C1Full;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static unsigned char buf[16384];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int bad;
    setvbuf(stdout, NULL, _IONBF, 0);
    live = (F_Crc32)GetProcAddress(h, "RtlCrc32");
    if (!live) { printf("resolve failed\n"); return 1; }

    bad = wia_crc32_tables_init();
    printf("== CORRECTNESS: RtlCrc32 ==\n");
    printf("   the shift tables check against their own definition: %s\n",
           bad ? "FAILED" : "0 disagreements");
    if (bad) { printf("CORRECTNESS: FAILED (the tables are wrong; nothing else matters)\n"); return 1; }

    {
        int i;
        for (i = 0; i < 16384; ++i) buf[i] = (unsigned char)(i * 131 + 7);
    }

    /* 1 and 2. every length, from zero and from a non-zero initial CRC */
    {
        long before = cases;
        int n;
        for (n = 0; n <= 4000; ++n) {
            one(buf, (SIZE_T)n, 0ul, "every length, init 0");
            one(buf, (SIZE_T)n, 0xDEADBEEFul, "every length, init non-zero");
        }
        printf("  1+2. every length 0..4000, from init 0 and from init 0xDEADBEEF -- crossing 192\n"
               "     and 3072, where the two block sizes begin, and every tail residue: %ld\n",
               cases - before);
    }

    /* 3. chaining across the block boundaries */
    {
        long before = cases;
        static const int SPLITS[14] = { 1, 7, 8, 63, 64, 65, 191, 192, 193, 1023, 1024, 3071, 3072, 3073 };
        int i, j, badc = 0;
        for (i = 0; i < 14; ++i)
            for (j = 0; j < 14; ++j) {
                SIZE_T n1 = (SIZE_T)SPLITS[i], n2 = (SIZE_T)SPLITS[j];
                ULONG whole = wia_crc32(buf, n1 + n2, 0x12345678ul);
                ULONG piece = wia_crc32(buf + n1, n2, wia_crc32(buf, n1, 0x12345678ul));
                ++cases;
                if (whole != piece) {
                    ++badc; ++fails;
                    if (fails <= 20)
                        printf("  CHAIN MISMATCH at %Iu + %Iu: %08lX vs %08lX\n",
                               n1, n2, (unsigned long)whole, (unsigned long)piece);
                }
                /* and the same split must agree with the live export */
                one(buf, n1 + n2, 0x12345678ul, "chained lengths");
            }
        printf("  3. chaining at 14x14 splits either side of every block boundary: %ld (%d bad)\n",
               cases - before, badc);
    }

    /* 4. a guard page */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 4, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize * 3, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  4. guard page SKIPPED\n");
        } else {
            int n, k;
            for (n = 0; n <= 4000; ++n) {
                unsigned char* p = (unsigned char*)(base + si.dwPageSize * 3) - n;
                for (k = 0; k < n; ++k) p[k] = (unsigned char)(k * 29 + 11);
                one(p, (SIZE_T)n, 0xA5A5A5A5ul, "guard page");
                ++guard;
            }
            printf("  4. the buffer ENDING at a PAGE_NOACCESS page, every length 0..4000: %ld\n",
                   guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* 5. randomised */
    {
        long before = cases;
        int trial, k;
        for (trial = 0; trial < 60000; ++trial) {
            SIZE_T n = (SIZE_T)(rnd() % 12000);
            ULONG init = rnd();
            for (k = 0; k < (int)n; ++k) buf[k] = (unsigned char)rnd();
            one(buf, n, init, "randomised");
        }
        printf("  5. randomised lengths, contents and initial values: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (exact vs a bitwise oracle AND the live export)\n");
    return fails ? 1 : 0;
}
