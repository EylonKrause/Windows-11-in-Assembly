/* changes/016-rtlunicodetoutf8n/measuring.c
 *
 * The measuring mode, added 2026-09-16 because it was missing, gated the same way as everything
 * else here: three-way against the live export, over a corpus built to reach every rule.
 *
 * RtlUnicodeToUTF8N(NULL, ...) asks how many bytes the output would need. This implementation did
 * not answer it, it returned STATUS_BUFFER_TOO_SMALL with nothing produced, and with a NULL
 * pointer and a NON-ZERO size it dereferenced the pointer and faulted. The conversion itself was
 * and is bit-exact; what was missing was a whole MODE of the function, which every corpus here
 * managed to miss because they all pass a real destination buffer.
 * See discovery/utf8n_null_destination.c for how it surfaced.
 *
 * What this file checks:
 *   1. the size AND the status against live, over every length from 0 to 400, for ASCII,
 *      two-byte, three-byte, surrogate-pair and lone-surrogate inputs;
 *   2. a NULL destination with a NON-ZERO size, which is the case that used to fault;
 *   3. that the measuring answer always agrees with what a real conversion produces, the size it
 *      reports must be exactly the size a big-enough buffer fills;
 *   4. randomised strings, including deliberately surrogate-heavy ones.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef LONG (NTAPI *F_ToUtf8N)(char*, ULONG, ULONG*, const wchar_t*, ULONG);
LONG wia_u2u8(char*, ULONG, ULONG*, const wchar_t*, ULONG);

static F_ToUtf8N live;
static long cases = 0, fails = 0, n_mapped = 0, n_notmapped = 0;

static wchar_t ws[512];
static char    big[4096];

static void one(int n, const char* where)
{
    ULONG gl = 0xDEAD, go = 0xDEAD, gc = 0xDEAD;
    LONG sl, so, sc;
    ++cases;
    sl = live(NULL, 0, &gl, ws, (ULONG)(n * 2));
    so = wia_u2u8(NULL, 0, &go, ws, (ULONG)(n * 2));
    if (sl == 0x107) ++n_notmapped; else ++n_mapped;

    /* and the measured size must be exactly what a real conversion produces */
    sc = wia_u2u8(big, sizeof big, &gc, ws, (ULONG)(n * 2));

    if (sl != so || gl != go || gc != go || (sc != so)) {
        if (++fails <= 20)
            printf("  MISMATCH [%s] n=%d: live %08lX/%lu  measured %08lX/%lu  converted %08lX/%lu\n",
                   where, n, (unsigned long)sl, (unsigned long)gl,
                   (unsigned long)so, (unsigned long)go, (unsigned long)sc, (unsigned long)gc);
    }
}

static unsigned long long rs = 0xCBBB9D5DC1059ED8ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int n, i, mode;
    setvbuf(stdout, NULL, _IONBF, 0);
    live = (F_ToUtf8N)GetProcAddress(h, "RtlUnicodeToUTF8N");
    if (!live) { printf("resolve failed\n"); return 1; }

    printf("== MEASURING MODE: RtlUnicodeToUTF8N(NULL, ...) ==\n");

    for (mode = 0; mode < 5; ++mode) {
        long before = cases;
        static const char* NAMES[5] = { "ASCII", "two-byte", "three-byte",
                                        "surrogate PAIRS", "LONE surrogates" };
        for (n = 0; n <= 400; ++n) {
            for (i = 0; i < n; ++i) {
                switch (mode) {
                case 0: ws[i] = (wchar_t)(L'a' + (i % 26)); break;
                case 1: ws[i] = (wchar_t)(0x80 + (i % 0x780)); break;
                case 2: ws[i] = (wchar_t)(0x800 + (i % 0x700)); break;
                case 3: ws[i] = (wchar_t)((i & 1) ? (0xDC00 + (i % 0x400))
                                                  : (0xD800 + (i % 0x400))); break;
                default: ws[i] = (wchar_t)(0xD800 + (i % 0x400)); break;   /* highs only */
                }
            }
            one(n, NAMES[mode]);
        }
        printf("  every length 0..400, %-16s: %ld\n", NAMES[mode], cases - before);
    }

    /* the case that used to fault */
    {
        ULONG gl = 0xDEAD, go = 0xDEAD;
        LONG sl, so;
        for (i = 0; i < 5; ++i) ws[i] = (wchar_t)(L'a' + i);
        sl = live(NULL, 99, &gl, ws, 10);
        so = wia_u2u8(NULL, 99, &go, ws, 10);
        ++cases;
        printf("  a NULL destination with a NON-ZERO size -- the case that used to FAULT:\n"
               "    live %08lX/%lu   ours %08lX/%lu   %s\n", (unsigned long)sl, (unsigned long)gl,
               (unsigned long)so, (unsigned long)go, (sl == so && gl == go) ? "agree" : "DIFFER");
        if (sl != so || gl != go) ++fails;
    }

    {
        long before = cases;
        int trial;
        for (trial = 0; trial < 120000; ++trial) {
            int len = (int)(rnd() % 300);
            int m = trial & 3;
            for (i = 0; i < len; ++i) {
                unsigned r = rnd();
                ws[i] = (m == 0) ? (wchar_t)(r % 128)
                      : (m == 1) ? (wchar_t)(r % 0x800)
                      : (m == 2) ? (wchar_t)(0xD000 + (r % 0x1200))
                                 : (wchar_t)r;
            }
            one(len, "randomised");
        }
        printf("  randomised, surrogate-heavy included: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf("  the live measuring mode answered SUCCESS %ld times and SOME_NOT_MAPPED %ld\n",
           n_mapped, n_notmapped);
    if (!n_notmapped) { printf("MEASURING: FAILED (no lone surrogate was ever measured)\n"); return 1; }
    printf(fails ? "MEASURING: FAILED\n"
                 : "MEASURING: PASS (size and status exact vs live, and equal to what a real\n"
                   "conversion produces)\n");
    return fails ? 1 : 0;
}
