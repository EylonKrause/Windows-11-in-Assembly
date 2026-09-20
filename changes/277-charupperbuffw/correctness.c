/* changes/277-charupperbuffw/correctness.c
 *
 * Gate 1 for user32!CharUpperBuffW and CharLowerBuffW: Ours vs the scalar model vs the live
 * EXPORTS, on the return value AND every character of the buffer.
 *
 * Both exports are one change, because they are the same loop with two tables and two ranges, and a
 * gate that checked only the upper form would leave the lower one's range, 'A'..'Z' plus 0x20
 * rather than 'a'..'z' minus it, untested. Change 268's lesson: a pair is one change or it is two
 * gates.
 *
 * The corpus is built where a blocked vector loop goes wrong:
 *
 *   * Every code unit 0..0xFFFF, alone and at every offset 0..31 inside a 32-character buffer. The
 *     loop switches between an in-register range subtract and a per-character table lookup based on
 *     whether ANY code unit in a 16-character block is at or above 0x80, so the position of a high
 *     code unit inside its block is exactly what decides which path runs.
 *
 *   * every LENGTH 0..200. The vector path runs while 32 bytes remain and the tail runs one
 *     character at a time, so every length crosses that boundary differently, and length 0 has its
 *     own rule, which is to touch nothing at all.
 *
 *   * The buffer is poisoned past the count and checked afterwards. CharUpperBuffW takes a count,
 *     not a terminator (probes/mapping.c measured it mapping straight past an embedded NUL) so
 *     an implementation that ran to a NUL, or that rounded the count up to a whole vector block,
 *     would corrupt whatever follows. That is change 016's defect exactly: storing sixteen and
 *     advancing by fewer.
 *
 *   * And a buffer ending exactly at a guard page, at every length, because a 32-byte load past
 *     the count is the way this loop would fault.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "user32.lib")

extern DWORD wia_charupperbuffw(wchar_t*, DWORD);
extern DWORD wia_charlowerbuffw(wchar_t*, DWORD);
extern int   wia_cub_init(void);
void ref_charupperbuffw(wchar_t*, DWORD);
void ref_charlowerbuffw(wchar_t*, DWORD);

static int  failures = 0;
static long cases = 0;

#define POISON 0x5A5A

/* Run one buffer through all three, on both directions, and compare everything. */
static void one(const wchar_t* src, DWORD n, int slack)
{
    static wchar_t a[1024], b[1024], c[1024];
    DWORD ra, rb;
    int dir, i, total = (int)n + slack;

    for (dir = 0; dir < 2; ++dir) {
        for (i = 0; i < total; ++i) a[i] = b[i] = c[i] = (i < (int)n) ? src[i] : (wchar_t)POISON;
        ++cases;
        if (dir == 0) {
            ra = wia_charupperbuffw(a, n);
            rb = CharUpperBuffW(b, n);
            ref_charupperbuffw(c, n);
        } else {
            ra = wia_charlowerbuffw(a, n);
            rb = CharLowerBuffW(b, n);
            ref_charlowerbuffw(c, n);
        }
        if (ra != rb || memcmp(a, b, (size_t)total * 2) != 0 ||
            memcmp(a, c, (size_t)total * 2) != 0) {
            if (failures < 12) {
                int k;
                printf("  FAIL %s n=%lu: returns %lu vs %lu\n", dir ? "lower" : "upper",
                       (unsigned long)n, (unsigned long)ra, (unsigned long)rb);
                for (k = 0; k < total && k < 8; ++k)
                    printf("       [%d] in %04X  ours %04X  live %04X  model %04X\n",
                           k, k < (int)n ? src[k] : POISON, a[k], b[k], c[k]);
            }
            ++failures;
        }
    }
}

int main(void)
{
    SYSTEM_INFO si;
    static wchar_t buf[1024];
    unsigned char* g_base;
    SIZE_T pagesz;
    int c, off, n, k;
    unsigned long seed = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    {
        int r = wia_cub_init();
        if (r) { printf("the case tables failed to build (%d)\n", r); return 1; }
    }
    printf("== CORRECTNESS: CharUpperBuffW and CharLowerBuffW ==\n");
    printf("  0. the two tables were built from the exports and agree with the range rule below 0x80\n");

    /* 1. every code unit, alone */
    {
        long before = cases;
        for (c = 0; c < 0x10000; ++c) {
            buf[0] = (wchar_t)c;
            one(buf, 1, 4);
        }
        printf("  1. every code unit 0..0xFFFF, alone: %ld\n", cases - before);
    }

    /* 2. every code unit at every offset 0..31 of a 32-character buffer, which block it lands in
          decides whether the vector path or the table path runs */
    {
        long before = cases;
        for (c = 0; c < 0x10000; c += 7) {          /* every seventh: 32 offsets each is 300k cases */
            for (off = 0; off < 32; ++off) {
                for (k = 0; k < 32; ++k) buf[k] = (wchar_t)(L'a' + (k % 26));
                buf[off] = (wchar_t)c;
                one(buf, 32, 4);
            }
        }
        printf("  2. a code unit at every offset 0..31 of a 32-character buffer: %ld\n",
               cases - before);
    }

    /* 3. every length 0..200, all-ASCII and with a high code unit in it */
    {
        long before = cases;
        for (n = 0; n <= 200; ++n) {
            for (k = 0; k < n; ++k) buf[k] = (wchar_t)(L'a' + (k % 26));
            one(buf, (DWORD)n, 8);
            for (k = 0; k < n; ++k) buf[k] = (wchar_t)(L'A' + (k % 26));
            one(buf, (DWORD)n, 8);
            if (n) {
                for (k = 0; k < n; ++k) buf[k] = (wchar_t)(L'a' + (k % 26));
                buf[n / 2] = (wchar_t)0x00E9;       /* one high code unit, mid-buffer */
                one(buf, (DWORD)n, 8);
                for (k = 0; k < n; ++k) buf[k] = (wchar_t)(0x0100 + (k % 0x400));
                one(buf, (DWORD)n, 8);             /* all high */
            }
        }
        printf("  3. every length 0..200, ASCII, high, and mixed: %ld\n", cases - before);
    }

    /* 4. embedded NULs; the count is what matters, not a terminator */
    {
        long before = cases;
        for (n = 1; n <= 64; ++n) {
            for (k = 0; k < n; ++k) buf[k] = (wchar_t)(L'a' + (k % 26));
            for (k = 0; k < n; ++k) {
                wchar_t save = buf[k];
                buf[k] = 0;
                one(buf, (DWORD)n, 4);
                buf[k] = save;
            }
        }
        printf("  4. a NUL at every position of every length 1..64: %ld\n", cases - before);
    }

    /* 5. a buffer ending exactly at a guard page */
    {
        long before = cases;
        GetSystemInfo(&si);
        pagesz = si.dwPageSize;
        g_base = (unsigned char*)VirtualAlloc(0, pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!g_base || !VirtualAlloc(g_base, pagesz, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  the guard page could not be set up\n"); return 1;
        }
        for (n = 0; n <= 200; ++n) {
            wchar_t* q = (wchar_t*)(g_base + pagesz - (SIZE_T)n * 2);
            /* The export's copy goes at the start of the page, not just before ours. The first
               version put it at `q - 64`, which for n >= 33 OVERLAPS q by two bytes -- so
               CharUpperBuffW rewrote the first character of our input and every length from 33 up
               "failed". The implementation was right and the test was measuring itself. */
            wchar_t* w = (wchar_t*)(g_base + 64);
            DWORD ra, rb;
            int i, bad = 0;
            /* ours and the export each get their own copy, both ending at the page edge */
            for (i = 0; i < n; ++i) q[i] = (wchar_t)((i & 3) ? (L'a' + (i % 26)) : (0x0100 + i));
            for (i = 0; i < n; ++i) w[i] = q[i];
            ra = wia_charupperbuffw(q, (DWORD)n);
            rb = CharUpperBuffW(w, (DWORD)n);
            ++cases;
            if (ra != rb) bad = 1;
            for (i = 0; i < n; ++i) if (q[i] != w[i]) { bad = 1; break; }
            if (bad) {
                if (failures < 12) printf("  FAIL guard n=%d\n", n);
                ++failures;
            }
        }
        printf("  5. a buffer ending exactly at a guard page, every length 0..200: %ld\n",
               cases - before);
    }

    /* 6. randomised */
    {
        long before = cases;
        for (k = 0; k < 40000 && failures < 12; ++k) {
            int i;
            seed = seed * 1103515245u + 12345u;
            n = (int)((seed >> 8) % 300);
            for (i = 0; i < n; ++i) {
                seed = seed * 1103515245u + 12345u;
                buf[i] = (wchar_t)(((seed >> 20) & 3) ? (L'a' + (seed % 26)) : (seed & 0xFFFF));
            }
            one(buf, (DWORD)n, 4);
        }
        printf("  6. 40000 randomised, length and content: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    if (!failures)
        printf("CORRECTNESS: PASS (the return value and every character exact vs live user32 and vs\n"
               "the scalar model, both directions, with the buffer poisoned past the count and a\n"
               "buffer ending exactly at a guard page)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
