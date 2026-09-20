/* changes/288-foldstringw-digits/probes/unrollhazard.c
 *
 * Why this probe exists: to turn an argument into a measurement.
 *
 * impl.asm drops its unroll when the buffers overlap, because probes/overlap.c showed the live export
 * behaving exactly like a naive forward one-unit-at-a-time loop and the unrolled loop does not always
 * reproduce that. Mutation mutant #28 weakened the overlap TEST, it measures the span in code units
 * instead of bytes, so it stops detecting overlap once the destination offset reaches half the length --
 * and it SURVIVED 66,656 correctness cases. The claim that it is an equivalent mutant rests on this:
 *
 *     the unrolled loop and the naive loop can only disagree when the destination offset is 1, 2 or 3,
 *
 * because impl.asm reads FOUR units, then writes those four, then reads the next four. A write can only
 * land on a unit that has not yet been read if it lands inside the same group of four, which needs an
 * offset of 1..3. At an offset of 4 or more every write of a group targets addresses the group has
 * already finished reading, so the two loops produce the same bytes. That, plus the fact that the
 * mutant's undetected region (offset >= n/2, only reachable when n >= 8, hence offset >= 4) is a subset
 * of the safe region, is the whole equivalence argument.
 *
 * An argument of that shape is exactly the kind that has been wrong before in this repository, so this
 * probe MEASURES the boundary instead. It implements both loops in C, in the same shape as impl.asm --
 * four loads, four table lookups, four stores, twice per iteration of eight, then a one-at-a-time tail --
 * and reports, for every length and every offset in both directions, whether they agree, and also
 * whether each agrees with the LIVE EXPORT. If the boundary is anywhere but 3, the argument is wrong and
 * mutant #28 is a real survivor rather than an equivalent one.
 *
 * It also re-measures the other equivalence claim, mutant #30: masking the unroll boundary with 15
 * instead of 7 moves the split between the unrolled body and the tail. Any split whose unrolled part is
 * a whole number of groups of eight must give the same answer, and that is checked for every split from
 * "everything unrolled" to "everything in the tail".
 *
 * build:  cl /nologo /O2 unrollhazard.c /Fe:unrollhazard.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define M_DIGITS 0x0080
#define N 512

static unsigned short tbl[65536];

static void build_table(void)
{
    int c;
    wchar_t in[2], out[8];
    for (c = 1; c < 65536; ++c) {
        in[0] = (wchar_t)c; in[1] = 0;
        if (FoldStringW(M_DIGITS, in, 1, out, 8) == 1) tbl[c] = (unsigned short)out[0];
        else tbl[c] = (unsigned short)c;
    }
    tbl[0] = 0;
}

/* The naive loop: one unit at a time, reading whatever is there NOW. This is what the live export does
   for overlapping buffers, measured in probes/overlap.c. */
static void naive(wchar_t* src, int n, wchar_t* dest)
{
    int i;
    for (i = 0; i < n; ++i) dest[i] = (wchar_t)tbl[(unsigned short)src[i]];
}

/* impl.asm's shape: `split` units go through the unrolled body (four loads, four lookups, four stores,
   twice), the rest through the one-at-a-time tail. split must be a multiple of eight. */
static void unrolled(wchar_t* src, int n, wchar_t* dest, int split)
{
    int i = 0;
    unsigned a, b, c, d;
    while (i < split) {
        a = (unsigned short)src[i];   b = (unsigned short)src[i+1];
        c = (unsigned short)src[i+2]; d = (unsigned short)src[i+3];
        dest[i] = (wchar_t)tbl[a];   dest[i+1] = (wchar_t)tbl[b];
        dest[i+2] = (wchar_t)tbl[c]; dest[i+3] = (wchar_t)tbl[d];
        a = (unsigned short)src[i+4]; b = (unsigned short)src[i+5];
        c = (unsigned short)src[i+6]; d = (unsigned short)src[i+7];
        dest[i+4] = (wchar_t)tbl[a]; dest[i+5] = (wchar_t)tbl[b];
        dest[i+6] = (wchar_t)tbl[c]; dest[i+7] = (wchar_t)tbl[d];
        i += 8;
    }
    for (; i < n; ++i) dest[i] = (wchar_t)tbl[(unsigned short)src[i]];
}

static void fill(wchar_t* p, int count)
{
    int i;
    for (i = 0; i < count; ++i) p[i] = (wchar_t)(0x0660 + (i % 10));   /* every unit folds */
}

int main(void)
{
    static wchar_t u[N], v[N], w[N];
    static const int lens[] = { 8, 9, 15, 16, 17, 24, 31, 32, 40, 64, 65, 96 };
    int li, off, i, disagree_max = -1, live_bad = 0, split_bad = 0;

    build_table();
    printf("== where does the unrolled loop diverge from the naive one on overlapping buffers? ==\n\n");

    /* ---- 1. the boundary, measured. dest above src. ---------------------------------------- */
    printf("-- 1. destination ABOVE the source. A row prints every offset at which the unrolled loop\n");
    printf("      and the naive loop differ. The equivalence argument for mutant #28 says that set is\n");
    printf("      always exactly {1, 2, 3}, independent of the length.\n\n");
    for (li = 0; li < (int)(sizeof(lens) / sizeof(lens[0])); ++li) {
        int n = lens[li], split = n - (n % 8);
        printf("      len %-3d  differ at offsets:", n);
        for (off = 1; off <= 40; ++off) {
            int base = 100;
            fill(u, N); fill(v, N);
            naive(u + base, n, u + base + off);
            unrolled(v + base, n, v + base + off, split);
            if (memcmp(u, v, sizeof(u))) {
                printf(" %d", off);
                if (off > disagree_max) disagree_max = off;
            }
        }
        printf("\n");
    }
    printf("\n      the largest offset at which they EVER differ: %d\n", disagree_max);
    printf("      %s\n", disagree_max == 3
           ? "so the hazard is confined to offsets 1..3, exactly as the argument requires"
           : "*** THE ARGUMENT IS WRONG: the hazard reaches further than offset 3 ***");

    /* ---- 2. dest below src -------------------------------------------------------------------- */
    printf("\n-- 2. destination BELOW the source. A forward loop writing to lower addresses can never\n");
    printf("      read a unit it has already overwritten, so there should be NO disagreement at all.\n\n");
    {
        int any = 0;
        for (li = 0; li < (int)(sizeof(lens) / sizeof(lens[0])); ++li) {
            int n = lens[li], split = n - (n % 8);
            for (off = 1; off <= 40; ++off) {
                int base = 100;
                fill(u, N); fill(v, N);
                naive(u + base, n, u + base - off);
                unrolled(v + base, n, v + base - off, split);
                if (memcmp(u, v, sizeof(u))) { printf("      len %d offset -%d DIFFERS\n", n, off); any = 1; }
            }
        }
        printf("      %s\n", any ? "*** they differ somewhere -- the argument is wrong ***"
                                 : "no length and no offset differs, as expected");
    }

    /* ---- 3. and both against the LIVE export, which is the only authority ------------------- */
    printf("\n-- 3. the same offsets against the LIVE export. The naive loop must match it everywhere\n");
    printf("      (that is what probes/overlap.c found); the unrolled loop must match it exactly\n");
    printf("      outside offsets 1..3.\n\n");
    for (li = 0; li < (int)(sizeof(lens) / sizeof(lens[0])); ++li) {
        int n = lens[li], split = n - (n % 8);
        for (off = 1; off <= 40; ++off) {
            int base = 100;
            fill(u, N); fill(v, N); fill(w, N);
            naive(u + base, n, u + base + off);
            unrolled(v + base, n, v + base + off, split);
            FoldStringW(M_DIGITS, w + base, n, w + base + off, N - base - off);
            if (memcmp(u, w, sizeof(u))) {
                printf("      len %d +%d: the NAIVE loop disagrees with the export\n", n, off);
                ++live_bad;
            }
            if (off > 3 && memcmp(v, w, sizeof(v))) {
                printf("      len %d +%d: the UNROLLED loop disagrees with the export past offset 3\n",
                       n, off);
                ++live_bad;
            }
        }
    }
    printf("      disagreements with the live export: %d\n", live_bad);

    /* ---- 4. mutant #30: every legal split of the same length must agree ---------------------- */
    printf("\n-- 4. mutant #30: the split between the unrolled body and the tail. Every split that is a\n");
    printf("      whole number of groups of eight must give the same answer on NON-overlapping buffers,\n");
    printf("      from fully unrolled down to fully scalar.\n\n");
    for (li = 0; li < (int)(sizeof(lens) / sizeof(lens[0])); ++li) {
        int n = lens[li], split;
        fill(u, N);
        naive(u, n, u + 200);                                  /* the reference answer, disjoint */
        for (split = 0; split <= n - (n % 8); split += 8) {
            fill(v, N);
            unrolled(v, n, v + 200, split);
            if (memcmp(u + 200, v + 200, (size_t)n * sizeof(wchar_t))) {
                printf("      len %d split %d DIFFERS\n", n, split);
                ++split_bad;
            }
        }
    }
    printf("      splits that differ: %d  %s\n", split_bad,
           split_bad ? "*** mutant #30 is NOT equivalent ***"
                     : "so any multiple-of-eight split is equivalent, mutant #30 included");

    printf("\n== verdict: hazard boundary %d, live disagreements %d, bad splits %d ==\n",
           disagree_max, live_bad, split_bad);
    for (i = 0; i < 1; ++i) (void)i;
    return (disagree_max == 3 && live_bad == 0 && split_bad == 0) ? 0 : 1;
}
