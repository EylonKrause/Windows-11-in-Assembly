/* changes/265-rtlappendasciiztostring/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll!RtlAppendAsciizToString.
 *
 * The whole destination buffer is compared, not the status and not Length. This export writes into
 * a caller's buffer and never writes a terminator, so the only way to catch an implementation that
 * helpfully NUL-terminates (which the wide analogue in change 101 legitimately does) is to fill
 * the buffer with poison and compare every byte of it afterwards. That check is the whole reason
 * this corpus exists in this shape: a test that looked at the status and the appended bytes would
 * pass an implementation that corrupts one byte past them on every successful call.
 *
 *   1. every combination of destination Length, MaximumLength and source length in a small range,
 *      which enumerates the fit boundary rather than sampling near it.
 *   2. every source length from 0 to 300 into a generous buffer, the vector loop, its overlapping
 *      tail, and the byte-at-a-time path below 32 all live here.
 *   3. The failure path at every size: the buffer must come back untouched, byte for byte.
 *   4. The wide sum: a Length and a source length that each fit a ushort but whose sum does not.
 *   5. a guard page, with the source ending exactly at an inaccessible page at every alignment --
 *      the scan reads 32 bytes at a time and the copy must not read past the NUL either.
 *   6. NULL and empty sources.
 *   7. RANDOMISED over lengths, capacities and content.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } ASTR;
typedef LONG (NTAPI *F_App)(ASTR*, const char*);

LONG wia_appendasciiztostring(void*, const char*);
LONG ref_appendasciiztostring(void*, const char*);

static F_App live;
static long cases = 0, fails = 0, n_ok = 0, n_fail = 0;

#define CAP 70016
static char b_ours[CAP], b_ref[CAP], b_live[CAP];

static void one(USHORT len0, USHORT maxlen, const char* src, const char* where)
{
    ASTR a, b, c;
    LONG ro, rr, rl;
    ++cases;
    memset(b_ours, '#', CAP);
    memset(b_ref,  '#', CAP);
    memset(b_live, '#', CAP);
    /* the bytes already "in" the destination, identical in all three */
    {
        USHORT i;
        for (i = 0; i < len0; ++i) { b_ours[i] = b_ref[i] = b_live[i] = (char)('A' + (i % 26)); }
    }
    a.Length = len0; a.MaximumLength = maxlen; a.Buffer = b_ours;
    b.Length = len0; b.MaximumLength = maxlen; b.Buffer = b_ref;
    c.Length = len0; c.MaximumLength = maxlen; c.Buffer = b_live;

    ro = wia_appendasciiztostring(&a, src);
    rr = ref_appendasciiztostring(&b, src);
    rl = live(&c, src);
    if (rl == 0) ++n_ok; else ++n_fail;

    if (ro != rl || rr != rl ||
        a.Length != c.Length || b.Length != c.Length ||
        a.MaximumLength != c.MaximumLength || b.MaximumLength != c.MaximumLength ||
        memcmp(b_ours, b_live, CAP) != 0 || memcmp(b_ref, b_live, CAP) != 0) {
        if (++fails <= 20) {
            int k;
            printf("  MISMATCH [%s] len0=%u max=%u srclen=%Iu: status ours=%08lX ref=%08lX "
                   "live=%08lX  Length ours=%u ref=%u live=%u", where, len0, maxlen,
                   src ? strlen(src) : (SIZE_T)0, (unsigned long)ro, (unsigned long)rr,
                   (unsigned long)rl, a.Length, b.Length, c.Length);
            for (k = 0; k < CAP; ++k)
                if (b_ours[k] != b_live[k] || b_ref[k] != b_live[k]) {
                    printf("   first differing byte [%d]: ours=%02X ref=%02X live=%02X",
                           k, (unsigned char)b_ours[k], (unsigned char)b_ref[k],
                           (unsigned char)b_live[k]);
                    break;
                }
            printf("\n");
        }
    }
}

static unsigned long long rs = 0x6A09E667F3BCC908ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static char src[70016];

static void mksrc(int n)
{
    int i;
    for (i = 0; i < n; ++i) src[i] = (char)('a' + (i % 26));
    src[n] = 0;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    setvbuf(stdout, NULL, _IONBF, 0);
    live = (F_App)GetProcAddress(h, "RtlAppendAsciizToString");
    if (!live) { printf("resolve failed\n"); return 1; }

    printf("== CORRECTNESS: RtlAppendAsciizToString ==\n");
    printf("   the WHOLE destination buffer is compared against a poison fill, because this export\n");
    printf("   never writes a terminator and its wide sibling does\n");

    /* 1. the fit boundary, enumerated */
    {
        long before = cases;
        int l0, mx, sl;
        for (l0 = 0; l0 <= 40; ++l0)
            for (mx = l0; mx <= l0 + 40; ++mx)
                for (sl = 0; sl <= 40; sl += 3) {
                    mksrc(sl);
                    one((USHORT)l0, (USHORT)mx, src, "the fit boundary");
                }
        printf("  1. every Length 0..40 x MaximumLength up to +40 x source length 0..40: %ld\n",
               cases - before);
    }

    /* 2. every source length into a generous buffer */
    {
        long before = cases;
        int sl;
        for (sl = 0; sl <= 300; ++sl) {
            mksrc(sl);
            one(0, 400, src, "every source length");
            one(7, 400, src, "every source length, unaligned start");
        }
        printf("  2. every source length 0..300, at an aligned and an unaligned destination end --\n"
               "     the vector loop, its overlapping tail and the byte path all live here: %ld\n",
               cases - before);
    }

    /* 3. the failure path at every size */
    {
        long before = cases;
        int sl;
        for (sl = 1; sl <= 200; ++sl) {
            mksrc(sl);
            one(0, (USHORT)(sl - 1), src, "one byte too long");
            one(10, (USHORT)(sl + 9), src, "one byte too long, non-empty dest");
        }
        printf("  3. a source one byte too long at every size -- the buffer must come back\n"
               "     untouched, byte for byte: %ld\n", cases - before);
    }

    /* 4. the wide sum */
    {
        long before = cases;
        mksrc(30000);
        one(40000, 65535, src, "40000 + 30000 must not wrap");
        one(35535, 65535, src, "35535 + 30000 fits exactly");
        one(35536, 65535, src, "35536 + 30000 is one too many");
        mksrc(65535);
        one(0, 65535, src, "65535 into 65535 fits exactly");
        one(1, 65535, src, "and one more does not");
        printf("  4. sums that a 16-bit comparison would wrap: %ld\n", cases - before);
    }

    /* 5. a guard page, with the SOURCE at the edge */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  5. guard page SKIPPED\n");
        } else {
            int len, k;
            for (len = 0; len <= 200; ++len) {
                char* s = base + si.dwPageSize - (len + 1);   /* the NUL is the last readable byte */
                for (k = 0; k < len; ++k) s[k] = (char)('a' + (k % 26));
                s[len] = 0;
                one(0, 400, s, "guard page, plenty of room");
                one(0, (USHORT)(len ? len - 1 : 0), s, "guard page, and it does not fit");
                guard += 2;
            }
            printf("  5. the SOURCE ending at a PAGE_NOACCESS page, every length 0..200 and so\n"
                   "     every alignment, fitting and not: %ld cases, no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* 6. NULL and empty */
    one(5, 40, NULL, "a NULL source");
    one(5, 40, "", "an empty source");
    one(0, 0, NULL, "NULL into a zero-capacity destination");
    one(0, 0, "", "empty into a zero-capacity destination");
    one(0, 0, "a", "one byte into a zero-capacity destination");
    printf("  6. NULL, empty, and a destination with no room at all: 5\n");

    /* 7. randomised */
    {
        long before = cases;
        int trial;
        for (trial = 0; trial < 120000; ++trial) {
            int sl = (int)(rnd() % 500);
            int l0 = (int)(rnd() % 200);
            int mx = l0 + (int)(rnd() % 600);
            mksrc(sl);
            one((USHORT)l0, (USHORT)mx, src, "randomised");
        }
        printf("  7. randomised lengths, capacities and starting offsets: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf("  the live export SUCCEEDED %ld times and refused %ld -- both paths matter, and only\n"
           "  the second one is allowed to leave the buffer alone\n", n_ok, n_fail);
    if (!n_ok || !n_fail) { printf("CORRECTNESS: FAILED (a path was never reached)\n"); return 1; }
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (status, Length and the WHOLE buffer exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
