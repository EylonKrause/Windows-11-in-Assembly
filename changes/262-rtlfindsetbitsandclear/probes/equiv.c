/* changes/262-rtlfindsetbitsandclear/probes/equiv.c
 *
 * Is the search half exactly RtlFindSetBits / RtlFindClearBits?
 *
 * contract.c showed a search that wraps, refuses a run straddling the wrap point, treats a hint
 * past the end as zero and returns the hint rounded down to eight for N = 0, which is, word for
 * word, what change 256 measured for the pure read-only pair. The obvious move is to build this
 * change on change 256's core. The obvious move is also how the eight-change space bug happened:
 * a rule was inherited from a sibling because it looked like the same rule, and it was wrong in
 * four landed changes before anyone enumerated it. Change 237 did this properly, it MEASURED
 * `PathIsPrefixA(a,b) == (PathCommonPrefixA(a,b,NULL) == strlen(a))` over 87 million pairs before
 * reusing change 236's scan, and this is the same test for this pair:
 *
 *     RtlFindSetBitsAndClear(bm, N, hint)  ==  RtlFindSetBits(bm, N, hint)
 *     RtlFindClearBitsAndSet(bm, N, hint)  ==  RtlFindClearBits(bm, N, hint)
 *
 * and, when the answer is not 0xFFFFFFFF, the mutation is exactly N bits at that index and
 * nothing else anywhere in the buffer.
 *
 * The read-only export is asked first, on an untouched copy, because the mutating one destroys the
 * evidence: calling them in the other order over one buffer would compare the second call against a
 * bitmap the first had already consumed, and they would disagree for a reason that has nothing to
 * do with the search.
 *
 * The whole buffer is diffed, not the N bits the answer points at. a mutation that also cleared one
 * bit somewhere else would be invisible to a check that only looked where it was told.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);

#define WORDS 128                                  /* 4096 bits */
static ULONG work[WORDS], copy[WORDS], expect[WORDS];

static unsigned long long rs = 0x9E3779B97F4A7C15ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static long n_search = 0, n_mut = 0, n_found = 0, n_notfound = 0, n_zero = 0;

static void check(F_Find mut, F_Find pure, int set_side, const char* what,
                  ULONG size, ULONG n, ULONG hint)
{
    ULONG r_pure, r_mut;
    ULONG i;
    RBM a, b;

    memcpy(copy, work, sizeof work);
    a.SizeOfBitMap = size; a.Buffer = copy;
    r_pure = pure(&a, n, hint);                    /* the read-only answer, on an untouched copy */

    memcpy(copy, work, sizeof work);
    b.SizeOfBitMap = size; b.Buffer = copy;
    r_mut = mut(&b, n, hint);                      /* ... and the mutating one, on its own copy */

    if (r_pure != r_mut) {
        if (n_search++ < 10)
            printf("  SEARCH DIFFERS [%s] size=%lu N=%lu hint=%lu: pure=%lu mutating=%lu\n",
                   what, size, n, hint, r_pure, r_mut);
        return;
    }

    /* what the buffer SHOULD look like afterwards */
    memcpy(expect, work, sizeof work);
    if (r_mut != 0xFFFFFFFFul && n != 0) {
        for (i = 0; i < n; ++i) {
            ULONG bit = r_mut + i;
            if (set_side) expect[bit >> 5] &= ~(1u << (bit & 31));   /* found SET   -> CLEARed */
            else          expect[bit >> 5] |=  (1u << (bit & 31));   /* found CLEAR -> SET */
        }
        ++n_found;
    } else if (n == 0) ++n_zero;
    else ++n_notfound;

    if (memcmp(expect, copy, sizeof expect) != 0) {
        if (n_mut++ < 10) {
            ULONG k;
            printf("  MUTATION DIFFERS [%s] size=%lu N=%lu hint=%lu -> %lu;  first wrong word:",
                   what, size, n, hint, r_mut);
            for (k = 0; k < WORDS; ++k)
                if (expect[k] != copy[k]) {
                    printf(" [%lu] expected %08lX got %08lX\n", k, expect[k], copy[k]);
                    break;
                }
        }
    }
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Find fsac = (F_Find)GetProcAddress(h, "RtlFindSetBitsAndClear");
    F_Find fcas = (F_Find)GetProcAddress(h, "RtlFindClearBitsAndSet");
    F_Find fsb  = (F_Find)GetProcAddress(h, "RtlFindSetBits");
    F_Find fcb  = (F_Find)GetProcAddress(h, "RtlFindClearBits");
    long trial;
    int i;

    if (!fsac || !fcas || !fsb || !fcb) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== IS THE SEARCH HALF EXACTLY RtlFindSetBits / RtlFindClearBits? ==\n");
    printf("   read-only asked FIRST on an untouched copy; the WHOLE buffer diffed afterwards\n\n");

    /* ---- 1. exhaustive-ish over small shapes, where the edges live ---- */
    {
        ULONG size, n, hint, at, len;
        for (at = 0; at <= 70; at += 7)
            for (len = 1; len <= 40; len += 3) {
                for (i = 0; i < WORDS; ++i) work[i] = 0u;
                for (i = 0; i < (int)len; ++i) work[(at + i) >> 5] |= 1u << ((at + i) & 31);
                for (size = 64; size <= 160; size += 32)
                    for (n = 0; n <= 42; n += 3)
                        for (hint = 0; hint <= 130; hint += 13) {
                            check(fsac, fsb, 1, "one planted run, SET side",  size, n, hint);
                            for (i = 0; i < WORDS; ++i) work[i] = ~work[i];
                            check(fcas, fcb, 0, "one planted run, CLEAR side", size, n, hint);
                            for (i = 0; i < WORDS; ++i) work[i] = ~work[i];
                        }
            }
    }
    printf("  1. one planted run at 11 positions x 14 lengths x 4 sizes x 15 N x 11 hints, "
           "both sides\n");

    /* ---- 2. randomised at several densities ---- */
    for (trial = 0; trial < 120000; ++trial) {
        ULONG size, n, hint;
        int d = (int)(rnd() % 5);
        for (i = 0; i < WORDS; ++i)
            work[i] = (d == 0) ? 0u : (d == 1) ? 0xFFFFFFFFu
                    : (d == 2) ? (ULONG)(rnd() | rnd())          /* mostly ones */
                    : (d == 3) ? (ULONG)(rnd() & rnd())          /* mostly zeros */
                               : (ULONG)rnd();
        size = 1 + (rnd() % (WORDS * 32));
        n    = rnd() % 80;
        if (rnd() & 3) n = 1 + (rnd() % 24);                     /* mostly findable sizes */
        hint = rnd() % (size + 64);
        check(fsac, fsb, 1, "randomised", size, n, hint);
        check(fcas, fcb, 0, "randomised", size, n, hint);
    }
    printf("  2. randomised, 5 densities, N 0..79, hints past the end: 240000 calls\n");

    /* ---- 3. the cases where the answer is the WRAPPED one ---- */
    for (trial = 0; trial < 20000; ++trial) {
        ULONG size = 1024, n = 1 + (rnd() % 16), at = rnd() % 200, hint = 300 + (rnd() % 700);
        ULONG len = n + (rnd() % 8);
        for (i = 0; i < WORDS; ++i) work[i] = 0u;
        for (i = 0; i < (int)len; ++i) work[(at + i) >> 5] |= 1u << ((at + i) & 31);
        check(fsac, fsb, 1, "the answer is BEHIND the hint", size, n, hint);
        for (i = 0; i < WORDS; ++i) work[i] = ~work[i];
        check(fcas, fcb, 0, "the answer is BEHIND the hint", size, n, hint);
    }
    printf("  3. the only run is BEHIND the hint, so the answer is the wrapped one: 40000 calls\n");

    printf("\n  search disagreements:   %ld\n", n_search);
    printf("  mutation disagreements: %ld\n", n_mut);
    printf("  (found %ld, not-found %ld, N=0 %ld -- all three arms exercised)\n",
           n_found, n_notfound, n_zero);
    if (n_search || n_mut) {
        printf("\nNOT EQUIVALENT -- change 256's core CANNOT simply be reused\n");
        return 1;
    }
    printf("\nEQUIVALENT: the search is exactly RtlFindSetBits / RtlFindClearBits, and the\n"
           "mutation is exactly N bits at the returned index and nothing else anywhere.\n");
    return 0;
}
