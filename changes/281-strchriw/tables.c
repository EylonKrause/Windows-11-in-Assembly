/* changes/281-strchriw/tables.c
 *
 * The match relation, indexed by needle, and checked against the live export.
 *
 * shlwapi!StrChrIW's equality relation was characterised by six probes in this directory, and the
 * property that shapes this file is the one that took longest to find:
 *
 *     U+D7B0 matches U+D7A2.  U+D7B1 matches U+D7A2.  U+D7B0 does NOT match U+D7B1.
 *
 * The relation is symmetric but not transitive. It is a tolerance relation, not an equivalence
 * relation, so it has NO CLASSES (168 intransitive triples) and the earlier version of this
 * file, which grouped code units into classes, was wrong in a way the gate caught: 66 mismatches in
 * 206096, every one of them a case where ours agreed with live and only the class model disagreed.
 *
 * What is well defined is the match set of a fixed needle, and that is all the implementation ever
 * needs. probes/gentable3.c enumerated it for all 65535 needles, scan, record, resume past the
 * hit, repeat, and split it by size:
 *
 *     10549933 matching pairs in total
 *     56825 needles match only THEMSELVES              -> nothing to store
 *     5390 needles have 2..8 partners                  -> foldsets.c, inline
 *     3320 needles have more than 8, between them
 *       sharing only eleven distinct sets              -> foldbig.c, as bitmaps
 *         3237 members (the ignorables, headed by U+00AD), 238, and nine of 12..25
 *
 * Three shapes come out of it, matching the three paths in impl.asm:
 *
 *   n[c] == 0     c matches only itself, one broadcast, one compare per 16 code units
 *   n[c] <= 4     the members are in the pool, four broadcasts, four compares
 *   n[c] <= 8     too many for the register budget, few enough to compare inline, scalar
 *   n[c] == 255   a bitmap: one load and one BT per code unit
 *
 * FOUR is the register budget, not a guess: Win64 makes xmm6-xmm15 non-volatile, so a leaf that
 * saves nothing has six YMM registers, one for the data, four for members, one scratch.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern const unsigned short wia_sci_sets[];
extern const unsigned wia_sci_nsetrows;
extern const unsigned wia_sci_nbig;
extern const unsigned wia_sci_bigcount[];
extern const unsigned short* const wia_sci_big[];
extern const unsigned short wia_sci_bigof[];
extern const unsigned wia_sci_nbigof;
extern const unsigned short wia_sci_nulhit[];
extern const unsigned wia_sci_nnulhit;

#define NSLOT 8192
#define NBMAP 16

unsigned char  wia_sci_n[65536];            /* 0 = self only, 1..8 = partners, 255 = bitmap */
unsigned short wia_sci_slot[65536];         /* index into the pool, 8 WORDs per slot */
unsigned short wia_sci_pool[8 * NSLOT];
unsigned char  wia_sci_bidx[65536];         /* 1-based bitmap index, 0 = none */
unsigned char  wia_sci_bmap[NBMAP][8192];   /* one bit per code unit */

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);

static int finds1(F_chr f, unsigned hay, unsigned needle)
{
    wchar_t s[2];
    s[0] = (wchar_t)hay; s[1] = 0;
    return f(s, (WCHAR)needle) != 0;
}

/* What the tables say: does `needle` match `w`? Exported, because reference.c asks exactly this
   question once per haystack character. Keeping the model on the same predicate but a different
   control flow means a bug in impl.asm's PATH SELECTION -- a needle routed to the vector path that
   should have used a bitmap -- shows up as a disagreement instead of being reproduced on both
   sides. */
int wia_sci_match(unsigned needle, unsigned w)
{
    unsigned n = wia_sci_n[needle], k;
    if (n == 0) return needle == w;
    if (n == 255) {
        unsigned b = wia_sci_bidx[needle];
        return b && ((wia_sci_bmap[b - 1][w >> 3] >> (w & 7)) & 1);
    }
    for (k = 0; k < n; ++k)
        if (wia_sci_pool[wia_sci_slot[needle] * 8 + k] == w) return 1;
    return 0;
}

/* Returns 0 on success. Non-zero means the tables and the live export disagree, and the gate must
   not run: every number in RESULTS.md would otherwise be about a different function. */
/* some w != c that matches c, or c itself when the needle matches nothing but itself. The gate
   uses this to build a corpus from the MEASURED relation rather than from a case function --
   building it from case functions is precisely what made probes/contract.c wrong. */
unsigned wia_sci_partner(unsigned c)
{
    unsigned n = wia_sci_n[c], k;
    if (n == 0) return c;
    if (n == 255) {
        unsigned b = wia_sci_bidx[c] - 1, w;
        for (w = 1; w <= 0xFFFF; ++w)
            if (w != c && ((wia_sci_bmap[b][w >> 3] >> (w & 7)) & 1)) return w;
        return c;
    }
    for (k = 0; k < n; ++k) {
        unsigned w = wia_sci_pool[wia_sci_slot[c] * 8 + k];
        if (w != c) return w;
    }
    return c;
}

int wia_sci_init(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    F_chr chrI = hs ? (F_chr)GetProcAddress(hs, "StrChrIW") : 0;
    unsigned i, k, next_slot = 0, pos;

    if (!chrI) return 1;
    if (wia_sci_nbig > NBMAP) return 2;

    /* 1. the small sets, inline */
    pos = 0;
    for (i = 0; i < wia_sci_nsetrows; ++i) {
        unsigned needle = wia_sci_sets[pos++];
        unsigned n = wia_sci_sets[pos++];
        unsigned slot;
        if (n < 1 || n > 8) return 3;
        if (next_slot >= NSLOT) return 4;
        slot = next_slot++;
        wia_sci_n[needle] = (unsigned char)n;
        wia_sci_slot[needle] = (unsigned short)slot;
        for (k = 0; k < n; ++k) wia_sci_pool[slot * 8 + k] = wia_sci_sets[pos++];
        /* pad with the first member so the vector path can always issue four compares */
        for (k = n; k < 8; ++k) wia_sci_pool[slot * 8 + k] = wia_sci_pool[slot * 8];
    }

    /* 2. the large sets, as membership bitmaps */
    for (i = 0; i < wia_sci_nbig; ++i)
        for (k = 0; k < wia_sci_bigcount[i]; ++k) {
            unsigned w = wia_sci_big[i][k];
            wia_sci_bmap[i][w >> 3] |= (unsigned char)(1u << (w & 7));
        }
    for (i = 0; i < wia_sci_nbigof; ++i) {
        unsigned needle = wia_sci_bigof[2 * i];
        unsigned idx = wia_sci_bigof[2 * i + 1];
        if (!idx || idx > wia_sci_nbig) return 5;
        wia_sci_n[needle] = 255;
        wia_sci_bidx[needle] = (unsigned char)idx;
    }

    /* 2b. The column this relation was extracted without.
     *
     * probes/gentable3.c searched a haystack holding every code unit 1..65535. It could not hold a
     * NUL, because StrChrIW stops at the terminator, so what matches code unit zero was
     * unreachable by construction, and the tables above say nothing about it. That is the same
     * failure as changes 097 and 100 and as this change's own probes/contract.c: a corpus that
     * could not express the case. The difference is where it sat, in the EXTRACTION METHOD rather
     * than in a test.
     *
     * Change 282's StrRChrIW has no terminator, so it can be asked, and its gate found the gap
     * immediately: 21 mismatches, every one a planted NUL the live export found and both our sides
     * missed. changes/282-strrchriw/probes/nulchar.c then asked every needle and wrote foldnul.c:
     * 3238 of them match a NUL, exactly the 3237 ignorables plus NUL itself, and symmetrically.
     *
     * All 3238 share one bitmap, so the fix is one bit, but that is a property of this data, not
     * a law, so it is CHECKED: every needle sharing a bitmap that gains bit 0 must itself be in the
     * list, and no needle outside the bitmap path may be in it. */
    {
        static unsigned char nul_bmap[NBMAP];
        static unsigned char in_list[65536];
        for (i = 0; i < wia_sci_nnulhit; ++i) {
            unsigned needle = wia_sci_nulhit[i];
            in_list[needle] = 1;
            if (wia_sci_n[needle] != 255) return 20;    /* not on the bitmap path: unhandled shape */
            nul_bmap[wia_sci_bidx[needle] - 1] = 1;
        }
        for (i = 1; i <= 0xFFFF; ++i)
            if (wia_sci_n[i] == 255 && nul_bmap[wia_sci_bidx[i] - 1] && !in_list[i])
                return 21;                              /* a bitmap shared across the boundary */
        for (i = 0; i < NBMAP; ++i)
            if (nul_bmap[i]) wia_sci_bmap[i][0] |= 1u;  /* code unit 0 joins the set */
    }

    /* ---- and now ask the export whether any of that is true -------------------------------- */

    /* 3. every stored partner must really be a partner, both ways, since the relation is symmetric */
    for (i = 1; i <= 0xFFFF; ++i) {
        unsigned n = wia_sci_n[i];
        if (n == 0 || n == 255) continue;
        for (k = 0; k < n; ++k) {
            unsigned w = wia_sci_pool[wia_sci_slot[i] * 8 + k];
            if (!finds1(chrI, w, i)) return 6;
            if (!finds1(chrI, i, w)) return 7;
        }
    }

    /* 4. a deterministic spread of pairs, checked against the export in both directions. This is
          the check that would catch a table claiming a match the export does not make -- the exact
          failure the class-based version had. */
    for (i = 0; i < 30000; ++i) {
        unsigned a = (i * 15373u + 7u) & 0xFFFF;
        unsigned b = (i * 40503u + 13u) & 0xFFFF;
        if (!a || !b) continue;
        if (wia_sci_match(a, b) != finds1(chrI, b, a)) return 8;
    }

    /* 5. and the specific facts that separate this relation from every hypothesis that was wrong */
    if (!wia_sci_match('a', 'A')) return 9;                 /* case is folded */
    if (wia_sci_match('a', 'b')) return 10;                 /* but not everything */
    if (!wia_sci_match('a', 0x1D2C)) return 11;             /* the pair contract.c could not ask */
    if (wia_sci_match('e', 0x00E9)) return 12;              /* accents stay distinct */
    if (!wia_sci_match(0xD7B0, 0xD7A2)) return 13;          /* the intransitive triple, all three */
    if (!wia_sci_match(0xD7B1, 0xD7A2)) return 14;
    if (wia_sci_match(0xD7B0, 0xD7B1)) return 15;           /* ...and its third leg must NOT hold */
    if (wia_sci_n[0x00AD] != 255) return 16;             /* the ignorables use a bitmap */
    /* Needle 0 Must not reach the singleton path. If it ever had a match set of one it would
       broadcast zero into the vector path and match the terminator, reporting the end of the string
       as a hit. It carries the ignorable set, so it takes the bitmap path -- but the implementation
       depends on that, so it is checked rather than assumed. */
    if (wia_sci_n[0] == 0) return 17;
    if (!wia_sci_match(0, 0x00AD)) return 18;            /* NUL matches the ignorables */
    if (wia_sci_match(0, L'a')) return 19;               /* and not ordinary characters */
    if (!wia_sci_match(0, 0)) return 22;                 /* AND A NUL MATCHES A NUL -- the column
                                                            change 281 could not reach */
    if (!wia_sci_match(0x00AD, 0)) return 23;            /* symmetrically */
    if (wia_sci_match(L'a', 0)) return 24;               /* but an ordinary needle does not */
    return 0;
}
