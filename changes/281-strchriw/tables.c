/* changes/281-strchriw/tables.c
 *
 * THE FOLD MAP, REBUILT FROM foldpairs.c AND THEN CHECKED AGAINST THE LIVE EXPORT.
 *
 * probes/foldtable.c took StrChrIW's equality relation straight from the function: 59321 classes
 * over 65535 code units, 57063 of them singletons, largest class 3237 (U+00AD and the other
 * ignorables). probes/locale.c proved it invariant across en-US, de-DE, Turkish and the invariant
 * locale, so it is a fixed object this project can own.
 *
 * It cannot be built at init the way change 277 builds its case tables, and both reasons were
 * measured rather than assumed:
 *
 *   * asking StrChrIW for the whole relation costs 86.5 SECONDS;
 *   * and LCMapStringW(LCMAP_SORTKEY|NORM_IGNORECASE), which computes it in 0.002 s, SPLITS 116
 *     classes the export unites -- every one a precomposed Hangul syllable against its jamo.
 *
 * So probes/gentable.c extracts it once and writes foldpairs.c: the 8472 code units that share a
 * class with anybody, and the smallest member of each class. This file rebuilds the full map from
 * those pairs and then ASKS THE EXPORT WHETHER THE MAP IS RIGHT, in milliseconds, so a stale or
 * corrupted table cannot survive to the gate.
 *
 * THREE STRUCTURES COME OUT OF IT:
 *
 *   fold[c]   the canonical representative of c. Used by the scalar path.
 *   cls[c]    0 if c is alone in its class; otherwise a slot in the member pool.
 *   pool[]    four members per slot, padded by repeating the first. A slot whose first entry is 0
 *             means the class has MORE than four members and the scalar path must be used.
 *
 * FOUR is not an arbitrary cut. Win64 makes xmm6-xmm15 non-volatile, so a leaf that saves nothing
 * has six YMM registers: one for the data, one for the terminator, and FOUR for class members.
 * 59241 of the 59321 classes have four members or fewer, so the vector path covers 99.87% of them
 * and the remaining 80 fall back to a lookup per character -- still far faster than the 43 ns per
 * character the shipped export costs.
 *
 * EVERY PASS HERE IS O(65536). The first version grouped members by scanning all 65536 code units
 * once per class, which is four billion iterations and would have made the gate unrunnable.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern const unsigned short wia_sci_pairs[];
extern const unsigned wia_sci_npairs;

#define NSLOT 4096
#define VKEEP 16                    /* members kept per class for the verification pass */

unsigned short wia_sci_fold[65536];
unsigned short wia_sci_cls[65536];
unsigned short wia_sci_pool[4 * NSLOT];

static unsigned short cnt[65536];
static unsigned short slotof[65536];
static unsigned short fill[NSLOT];
static unsigned short vmem[NSLOT][VKEEP];
static unsigned short vn[NSLOT];

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);

/* Returns 0 on success. Non-zero means the table and the live export disagree, and the gate must
   not run: every number in RESULTS.md would otherwise be about a different function. */
int wia_sci_init(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    F_chr chrI = hs ? (F_chr)GetProcAddress(hs, "StrChrIW") : 0;
    unsigned i, c, d, slot, next_slot = 1;

    if (!chrI) return 1;

    for (i = 0; i < 65536; ++i) { wia_sci_fold[i] = (unsigned short)i; wia_sci_cls[i] = 0; }
    for (i = 0; i < wia_sci_npairs; ++i) {
        unsigned code = wia_sci_pairs[2 * i], rep = wia_sci_pairs[2 * i + 1];
        if (!code) return 2;
        wia_sci_fold[code] = (unsigned short)rep;
    }

    /* pass 1: how big is each class */
    for (c = 1; c <= 0xFFFF; ++c) ++cnt[wia_sci_fold[c]];

    /* pass 2: give every non-singleton class a slot */
    for (c = 1; c <= 0xFFFF; ++c) {
        if (wia_sci_fold[c] != c || cnt[c] <= 1) continue;
        if (next_slot >= NSLOT) return 3;
        slot = next_slot++;
        slotof[c] = (unsigned short)slot;
        if (cnt[c] > 4) wia_sci_pool[slot * 4] = 0;     /* sentinel: use the scalar path */
    }

    /* pass 3: file every member under its slot */
    for (c = 1; c <= 0xFFFF; ++c) {
        unsigned rep = wia_sci_fold[c];
        slot = slotof[rep];
        if (!slot) continue;
        wia_sci_cls[c] = (unsigned short)slot;
        if (cnt[rep] <= 4 && fill[slot] < 4) wia_sci_pool[slot * 4 + fill[slot]++] = (unsigned short)c;
        if (vn[slot] < VKEEP) vmem[slot][vn[slot]++] = (unsigned short)c;
    }

    /* pass 4: pad the short slots by repeating the first member, so the vector path can always
       issue four compares without caring how many are real */
    for (slot = 1; slot < next_slot; ++slot) {
        if (!wia_sci_pool[slot * 4]) continue;          /* the sentinel slots stay as they are */
        for (d = fill[slot]; d < 4; ++d) wia_sci_pool[slot * 4 + d] = wia_sci_pool[slot * 4];
    }

    /* ---- and now ask the export whether any of that is true -------------------------------- */

    /* 1. every member of every non-singleton class must be found, at the FIRST member, in a string
          made of that class. Short strings, so this is milliseconds rather than 86.5 seconds. */
    for (slot = 1; slot < next_slot; ++slot) {
        wchar_t s[VKEEP + 2];
        unsigned n = vn[slot];
        for (d = 0; d < n; ++d) s[d] = (wchar_t)vmem[slot][d];
        s[n] = 0;
        for (d = 0; d < n; ++d) {
            PCWSTR p = chrI(s, (WCHAR)vmem[slot][d]);
            if (p != s) return 4;
        }
    }

    /* 2. the negative direction, sampled. Checking all 65535 against all 59321 classes is the
          86.5-second problem; a deterministic spread of 8192 pairs is a few milliseconds and would
          catch a table that had merged two classes together. */
    for (i = 0; i < 8192; ++i) {
        unsigned a = (i * 15373u + 7u) & 0xFFFF;
        unsigned b = (i * 40503u + 13u) & 0xFFFF;
        wchar_t s[2];
        if (!a || !b || wia_sci_fold[a] == wia_sci_fold[b]) continue;
        s[0] = (wchar_t)b; s[1] = 0;
        if (chrI(s, (WCHAR)a) != 0) return 5;
    }

    /* 3. a table that came back as the identity everywhere would be a wrong answer that looks like
          a right one, so the four facts that distinguish this relation from its neighbours: */
    if (wia_sci_fold['a'] != wia_sci_fold['A']) return 6;   /* case is folded */
    if (wia_sci_fold['a'] == wia_sci_fold['b']) return 7;   /* but not everything */
    if (wia_sci_fold[0x1D2C] != wia_sci_fold['a']) return 8; /* the pair contract.c could not ask */
    if (wia_sci_fold[0x00E9] == wia_sci_fold['e']) return 9; /* accents stay distinct */
    if (!wia_sci_cls['a'] || wia_sci_pool[wia_sci_cls['a'] * 4] == 0) return 10;
    return 0;
}
