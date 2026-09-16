/* changes/034-rtlutf8tounicoden/measuring.c
 *
 * THE MEASURING MODE, added 2026-09-16 because it was missing, gated the same way as everything
 * else here: three-way against the live export, over a corpus built to reach every rule.
 *
 * RtlUTF8ToUnicodeN(NULL, ...) asks how many BYTES of UTF-16 the output would need. This
 * implementation did not answer it -- it returned STATUS_BUFFER_TOO_SMALL with nothing produced,
 * and with a NULL pointer and a NON-ZERO size it dereferenced the pointer and faulted. The decoder
 * itself was and is bit-exact; what was missing was a whole MODE of the function, which every
 * corpus here managed to miss because they all pass a real destination buffer.
 * See discovery/utf8n_null_destination.c for how it surfaced.
 *
 * MALFORMED INPUT IS THE WHOLE DIFFICULTY, and it is why this file leans on it so heavily. The
 * count is not "one replacement per bad byte": probes/policy.c read the rule off the shipped
 * decoder one hand-built sequence at a time, and it consumes a MAXIMAL subpart per replacement --
 * with the twist this decoder already documents, that a byte-2 which is a generic continuation but
 * outside the lead's own range is consumed (one replacement, two bytes) while a byte-2 that is not
 * a continuation at all is not. A size one unit short truncates a caller's string.
 *
 * WHAT THIS FILE CHECKS:
 *   1. the size AND the status against live, over every length from 0 to 300, for ASCII, valid
 *      two-, three- and four-byte sequences, continuation runs, lead runs and fully random bytes;
 *   2. the hand-built malformed sequences from probes/policy.c, which are the cases the rule was
 *      derived from -- truncations, interrupted leads, out-of-range byte-2, invalid leads;
 *   3. a NULL destination with a NON-ZERO size, which is the case that used to fault;
 *   4. that the measured size is exactly the size a real conversion produces.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef LONG (NTAPI *F_ToUniN)(wchar_t*, ULONG, ULONG*, const char*, ULONG);
LONG wia_u82u(wchar_t*, ULONG, ULONG*, const char*, ULONG);

static F_ToUniN live;
static long cases = 0, fails = 0, n_ok = 0, n_notmapped = 0;

static unsigned char bs[512];
static wchar_t big[4096];

static void one(int n, const char* where)
{
    ULONG gl = 0xDEAD, go = 0xDEAD, gc = 0xDEAD;
    LONG sl, so, sc;
    ++cases;
    sl = live(NULL, 0, &gl, (const char*)bs, (ULONG)n);
    so = wia_u82u(NULL, 0, &go, (const char*)bs, (ULONG)n);
    if (sl == 0x107) ++n_notmapped; else ++n_ok;
    sc = wia_u82u(big, sizeof big, &gc, (const char*)bs, (ULONG)n);
    if (sl != so || gl != go || gc != go || sc != so) {
        if (++fails <= 20) {
            int i;
            printf("  MISMATCH [%s] n=%d: live %08lX/%lu  measured %08lX/%lu  converted %08lX/%lu"
                   "  bytes:", where, n, (unsigned long)sl, (unsigned long)gl,
                   (unsigned long)so, (unsigned long)go, (unsigned long)sc, (unsigned long)gc);
            for (i = 0; i < n && i < 12; ++i) printf(" %02X", bs[i]);
            printf("\n");
        }
    }
}

static unsigned long long rs = 0x9B05688C2B3E6C1Full;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int n, i, mode;
    setvbuf(stdout, NULL, _IONBF, 0);
    live = (F_ToUniN)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    if (!live) { printf("resolve failed\n"); return 1; }

    printf("== MEASURING MODE: RtlUTF8ToUnicodeN(NULL, ...) ==\n");

    for (mode = 0; mode < 6; ++mode) {
        long before = cases;
        static const char* NAMES[6] = { "ASCII", "continuation runs", "C0-FF lead runs",
                                        "E0-FF lead runs", "fully random", "valid UTF-8" };
        for (n = 0; n <= 300; ++n) {
            for (i = 0; i < n; ++i) {
                unsigned r = (unsigned)(i * 2654435761u + mode);
                switch (mode) {
                case 0: bs[i] = (unsigned char)(0x61 + (r % 26)); break;
                case 1: bs[i] = (unsigned char)(0x80 + (r % 0x40)); break;
                case 2: bs[i] = (unsigned char)(0xC0 + (r % 0x40)); break;
                case 3: bs[i] = (unsigned char)(0xE0 + (r % 0x20)); break;
                case 4: bs[i] = (unsigned char)r; break;
                default:
                    if ((r & 3) == 0 && i + 1 < n) { bs[i] = 0xC3; bs[++i] = 0xA9; }
                    else if ((r & 7) == 1 && i + 2 < n) { bs[i] = 0xE2; bs[i+1] = 0x82; bs[i+2] = 0xAC; i += 2; }
                    else if ((r & 15) == 3 && i + 3 < n) { bs[i] = 0xF0; bs[i+1] = 0x9F; bs[i+2] = 0x98; bs[i+3] = 0x80; i += 3; }
                    else bs[i] = (unsigned char)(0x61 + (r % 26));
                    break;
                }
            }
            one(n, NAMES[mode]);
        }
        printf("  every length 0..300, %-18s: %ld\n", NAMES[mode], cases - before);
    }

    /* the hand-built sequences the rule was derived from */
    {
        long before = cases;
        static const unsigned char SEQ[][6] = {
            {0x80}, {0x80,0x80}, {0x80,0x80,0x80}, {0x61,0x80,0x62},
            {0xC3}, {0xE2,0x82}, {0xE2}, {0xF0,0x9F,0x98}, {0xF0,0x9F}, {0xF0},
            {0xE2,0x41}, {0xE2,0x82,0x41}, {0xF0,0x41}, {0xF0,0x9F,0x41}, {0xC3,0x41},
            {0xC0,0xAF}, {0xC1,0xBF}, {0xF5,0x80,0x80,0x80}, {0xFE}, {0xFF,0xFF},
            {0xED,0xA0,0x80}, {0xE0,0x80,0xAF}, {0xF4,0x90,0x80,0x80},
            {0x61}, {0xC3,0xA9}, {0xE2,0x82,0xAC}, {0xF0,0x9F,0x98,0x80}
        };
        static const int LENS[27] = { 1,2,3,3, 1,2,1,3,2,1, 2,3,2,3,2, 2,2,4,1,2, 3,3,4, 1,2,3,4 };
        int k;
        for (k = 0; k < 27; ++k) {
            for (i = 0; i < LENS[k]; ++i) bs[i] = SEQ[k][i];
            one(LENS[k], "the derived sequences");
        }
        printf("  the hand-built malformed sequences the rule came from: %ld\n", cases - before);
    }

    /* the case that used to fault */
    {
        ULONG gl = 0xDEAD, go = 0xDEAD;
        LONG sl, so;
        for (i = 0; i < 5; ++i) bs[i] = (unsigned char)('a' + i);
        sl = live(NULL, 99, &gl, (const char*)bs, 5);
        so = wia_u82u(NULL, 99, &go, (const char*)bs, 5);
        ++cases;
        printf("  a NULL destination with a NON-ZERO size -- the case that used to FAULT:\n"
               "    live %08lX/%lu   ours %08lX/%lu   %s\n", (unsigned long)sl, (unsigned long)gl,
               (unsigned long)so, (unsigned long)go, (sl == so && gl == go) ? "agree" : "DIFFER");
        if (sl != so || gl != go) ++fails;
    }

    {
        long before = cases;
        int trial;
        for (trial = 0; trial < 150000; ++trial) {
            int len = (int)(rnd() % 250);
            int m = trial % 5;
            for (i = 0; i < len; ++i) {
                unsigned r = rnd();
                bs[i] = (m == 0) ? (unsigned char)(r % 128)
                      : (m == 1) ? (unsigned char)(0x80 + (r % 0x40))
                      : (m == 2) ? (unsigned char)(0xC0 + (r % 0x40))
                      : (m == 3) ? (unsigned char)(0xE0 + (r % 0x20))
                                 : (unsigned char)r;
            }
            one(len, "randomised");
        }
        printf("  randomised over five alphabets: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf("  the live measuring mode answered SUCCESS %ld times and SOME_NOT_MAPPED %ld\n",
           n_ok, n_notmapped);
    if (!n_notmapped || !n_ok) { printf("MEASURING: FAILED (a status was never produced)\n"); return 1; }
    printf(fails ? "MEASURING: FAILED\n"
                 : "MEASURING: PASS (size and status exact vs live, and equal to what a real\n"
                   "conversion produces)\n");
    return fails ? 1 : 0;
}
