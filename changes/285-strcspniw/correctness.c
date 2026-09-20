/* changes/285-strcspniw/correctness.c
 *
 * Three-way: ours vs an independent scalar model vs the LIVE shlwapi export.
 *
 * The corpus shapes are inherited from changes 283 And 284 On purpose. Each of those changes ended up
 * with corpora that exist only because a mutant survived, and two of them caught real defects in a
 * shipped implementation rather than in a mutant. Rebuilding this gate from the "obvious" cases would
 * walk into the same holes, so the shapes come over from the start and are adapted to a SET-based span:
 *
 *   * every alignment, because the block scan masks the bytes below the string pointer;
 *   * a match planted where the scan must NOT find it -- below the string pointer;
 *   * Every member of every dispatch class of the relation, because this change expands a set member's
 *     whole pool slot and only the last member of a slot exposes an off-by-one there;
 *   * the guard page with the terminator as the last readable code unit;
 *   * a NON-ZERO buffer after the terminator, so "it never reads past" is proved rather than assumed;
 *   * and the boundaries of this change's own structure: the chunk size of four and the accept-list
 *     cap of sixteen, each tested at, just below and just above.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (WINAPI *FSPN)(PCWSTR, PCWSTR);

int wia_strcspniw(const wchar_t* str, const wchar_t* set);
int ref_strcspniw(const wchar_t* str, const wchar_t* set);
int wia_sci_init(void);
int wia_sci_match(unsigned needle, unsigned w);
extern const unsigned char wia_sci_n[];

static FSPN sys;
static long cases, n_early, n_full;
static int failures;

#define SHY  0x00AD     /* SOFT HYPHEN -- one of the 3237 ignorables, and matches a NUL */
#define CGJ  0x034F     /* COMBINING GRAPHEME JOINER -- another member of that same set */
#define ZWSP 0x200B     /* ZERO WIDTH SPACE -- matches ONLY ITSELF (n = 0) */

/* The filler, and why it is checked rather than chosen.
 *
 * Three corpora in this file were written with a filler that turned out to be IN the set under test,
 * which makes every answer 0 and the whole corpus vacuous -- it passes, it proves nothing, and the
 * mutant it was written to catch walks straight through it:
 *
 *   * corpus 14 filled with 'z' against a set spanning U+004D..U+0060, and 'z' matches 'Z';
 *   * corpora 7, 9 and 16 filled with U+FFFD, which has n = 255 and is itself one of the 3237
 *     ignorables -- so it matched the SOFT HYPHEN member those corpora deliberately included.
 *
 * A code unit with n = 0 matches only itself, and the relation is symmetric, so such a unit is in no
 * other member's set. U+0002 is one, and vacuity_check() proves it at startup against every set this
 * file uses. plant() then re-proves it PER CASE: before planting anything it asks the live export what
 * the untouched string gives, and that must be the full length. */
#define FILL 0x0002

static void one(const wchar_t* s, const wchar_t* set)
{
    int a = wia_strcspniw(s, set);
    int b = sys(s, set);
    int c = ref_strcspniw(s, set);
    ++cases;
    {
        int len = 0;
        while (s && s[len]) ++len;
        if (b < len) ++n_early; else ++n_full;
    }
    if (a != b || a != c) {
        if (failures < 12)
            printf("  FAIL setlen=%d: ours %d  live %d  model %d\n",
                   (int)(set ? wcslen(set) : 0), a, b, c);
        ++failures;
    }
}

/* Plant `c` at `pos` in a FILL-filled string of length `len` and compare three ways -- but first
   confirm the untouched string answers `len`, because a filler that is itself in the set makes the
   case vacuous. That has happened three times in this file, and it always passed. */
static void plant(wchar_t* s, int len, int pos, wchar_t c, const wchar_t* set)
{
    int ctrl;
    s[pos] = (wchar_t)FILL;
    s[len] = 0;
    ctrl = sys(s, set);
    if (ctrl != len) {
        if (failures < 12)
            printf("  VACUOUS: the filler is in the set -- untouched string gives %d, length is %d\n",
                   ctrl, len);
        ++failures;
        return;
    }
    one(s, set);                    /* the control itself is a case worth counting */
    s[pos] = c;
    one(s, set);
    s[pos] = (wchar_t)FILL;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    SYSTEM_INFO si;
    unsigned char* base;
    SIZE_T pg;
    int k, j, m;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (FSPN)GetProcAddress(hs, "StrCSpnIW");
    if (!sys) { printf("no StrCSpnIW\n"); return 2; }
    printf("== CORRECTNESS: shlwapi!StrCSpnIW ==\n");
    if (wia_sci_init()) { printf("  change 281's tables disagree with the live export\n"); return 1; }
    printf("  0. change 281's match relation rebuilt and re-checked against the live export\n");
    {
        /* FILL must match only itself, or every planting corpus below is vacuous. */
        static const unsigned short used[] = { 0x0001, 0x0020, 0x0023, 0x0035, 0x004B, 0x00C6,
                                               0x0598, 0x02B9, 0x00AD, 0x034F, 0x200B, 0xD7A2,
                                               0x030D, 0x0591, 0x1CD0, 0x27F8, 0x3005, 0xB9C6,
                                               0xC0AA, 0xC542, 0xC78E };
        int i, bad = 0;
        if (wia_sci_n[FILL] != 0) { printf("  FILLER U+%04X is not self-only\n", FILL); ++failures; }
        for (i = 0; i < (int)(sizeof(used) / sizeof(used[0])); ++i)
            if (wia_sci_match(used[i], FILL)) {
                printf("  FILLER U+%04X is accepted by U+%04X\n", FILL, used[i]); ++bad;
            }
        if (bad) ++failures;
        printf("  0b. the filler U+%04X matches only itself and is in none of the %d sets this\n"
               "      corpus uses -- checked, because three corpora here were once vacuous for\n"
               "      exactly that reason\n", FILL, (int)(sizeof(used) / sizeof(used[0])));
    }

    /* 1. the match at every position of a 40-character string, for sets of 1..5 characters */
    {
        long before = cases;
        static wchar_t s[48], set[8];
        for (j = 1; j <= 5; ++j) {
            for (m = 0; m < 40; ++m) {
                for (k = 0; k < 40; ++k) s[k] = L'z';
                s[40] = 0;
                for (k = 0; k < j; ++k) set[k] = (wchar_t)(L'A' + k);
                set[j] = 0;
                s[m] = (wchar_t)(L'a' + (m % j));    /* a member of the set, at m */
                one(s, set);
                s[m] = L'z';
                one(s, set);                         /* and with nothing in the set at all */
            }
        }
        printf("  1. the match at every position of 40 characters, sets of 1..5: %ld\n",
               cases - before);
    }

    /* 2. every alignment of the string pointer within a 32-byte block */
    {
        long before = cases;
        static wchar_t pad[160];
        for (k = 0; k < 32; ++k) {
            wchar_t* s = pad + 32 + k;
            for (j = 0; j < 160; ++j) pad[j] = L'z';
            for (m = 0; m < 24; ++m) {
                for (j = 0; j < 40; ++j) s[j] = L'z';
                s[40] = 0;
                s[m] = L'q';
                one(s, L"Q");
                one(s, L"QX");
                one(s, L"XQ");
            }
        }
        printf("  2. every alignment x every match position, 1- and 2-character sets: %ld\n",
               cases - before);
    }

    /* 3. a match planted BELOW the string pointer, at every alignment.
     *
     * The block scan reads aligned 32-byte blocks, so the block holding the string pointer almost
     * always extends below it, and the bottom edge mask is the only thing stopping a hit there from
     * being accepted. Change 283's equivalent corpus caught exactly that mutant.
     */
    {
        long before = cases;
        static wchar_t buf[128];
        for (k = 0; k < 32; ++k) {
            wchar_t* s = buf + 32 + k;
            for (j = 0; j < 128; ++j) buf[j] = L'z';
            buf[5] = L'q'; buf[12] = L'q'; buf[20] = L'q'; buf[31] = L'q';
            s[30] = 0;
            one(s, L"Q");                            /* every 'q' is below `s`: the full length */
            s[9] = L'q';
            one(s, L"Q");                            /* and the control, above it */
            s[9] = L'z';
        }
        printf("  3. a match planted BELOW the string pointer at all 32 alignments, with its\n"
               "     control above it: %ld\n", cases - before);
    }

    /* 4. an embedded NUL at every position ends the scan */
    {
        long before = cases;
        static wchar_t s[24];
        for (k = 1; k < 20; ++k) {
            for (m = 0; m < 20; ++m) s[m] = (wchar_t)(L'a' + m);
            s[20] = 0;
            s[k] = 0;
            one(s, L"T");                            /* 't' is at 19, behind the NUL */
            one(s, L"B");
            one(s, L"xyz");
            one(s, L"");
        }
        printf("  4. an embedded NUL at every position ends the scan: %ld\n", cases - before);
    }

    /* 5. the ignorables and the intransitive triple, on the SET side.
     *
     * 0x00AD and 0x034F really are two members of the same 3237-code-unit set; 0x200B matches only
     * itself, and saying otherwise is the mistake that made two earlier corpora test nothing.
     */
    {
        long before = cases;
        static wchar_t s[8], set[4];
        s[0] = L'a'; s[1] = L'b'; s[2] = CGJ; s[3] = L'c'; s[4] = 0;
        set[0] = SHY; set[1] = 0;
        one(s, set);                                 /* 2: a real ignorable pair */
        s[2] = ZWSP;
        one(s, set);                                 /* 4: 0x200B is NOT in that set */
        set[0] = ZWSP;
        one(s, set);                                 /* 2: but it matches itself */
        s[0] = L'x'; s[1] = 0xD7B0; s[2] = 0;
        set[0] = 0xD7A2; set[1] = 0;
        one(s, set);                                 /* 1 */
        s[1] = 0xD7B1;
        one(s, set);                                 /* 1 */
        set[0] = 0xD7B0;
        one(s, set);                                 /* 2: these two do not match each other */
        s[1] = 0xD7A2;
        one(s, set);                                 /* 1: symmetric */
        printf("  5. the ignorables (a REAL pair) and the intransitive triple from the set side: "
               "%ld\n", cases - before);
    }

    /* 6. degenerate arguments */
    {
        long before = cases;
        static wchar_t t[] = L"abcXYZabc";
        static wchar_t e[] = L"";
        one(t, L"");                                 /* the empty set: the full length */
        one(e, L"abc");                              /* the empty string: 0 */
        one(e, L"");
        one(t, L"Q");                                /* nothing matches: the full length */
        one(t, L"A");                                /* the very first character: 0 */
        {
            int a = wia_strcspniw(0, L"a"), b = sys(0, L"a"), c = ref_strcspniw(0, L"a");
            ++cases; ++n_full;
            if (a != b || a != c) { printf("  FAIL: NULL string gave %d/%d/%d\n", a, b, c); ++failures; }
        }
        {
            int a = wia_strcspniw(t, 0), b = sys(t, 0), c = ref_strcspniw(t, 0);
            ++cases; ++n_full;
            if (a != b || a != c) { printf("  FAIL: NULL set gave %d/%d/%d\n", a, b, c); ++failures; }
        }
        printf("  6. empty set, empty string, no match, first character, NULL arguments: %ld\n",
               cases - before);
    }

    /* 7. Every member of every dispatch class, as a set member and in the string.
     *
     * A set member with 2..8 partners has its whole pool slot copied into the accept list, so an
     * off-by-one there is only visible through the LAST member of the slot. probes/relation.c (via
     * change 283's probes/partners.c) established that only nine counts occur: 0, 2, 3, 4, 5, 6, 7, 8
     * and the 255 bitmap sentinel -- and 5..8 are the counts that cross this change's chunk of four.
     */
    {
        long before = cases;
        static const unsigned short reps[] = { 0x0001, 0x0020, 0x0023, 0x0035,
                                               0x004B, 0x00C6, 0x0598, 0x02B9, 0x00AD };
        static wchar_t s[48], set[4];
        unsigned r, mem;
        int i, seen;
        for (i = 0; i < 9; ++i) {
            r = reps[i];
            seen = 0;
            set[0] = (wchar_t)r; set[1] = 0;
            for (mem = 1; mem < 65536; ++mem) {
                if (!wia_sci_match(r, mem)) continue;
                ++seen;
                if (r == 0x00AD && (seen % 101) != 1) continue;   /* 3237 members: sample */
                for (k = 0; k < 40; ++k) s[k] = (wchar_t)FILL;
                plant(s, 40, 25, (wchar_t)mem, set);              /* 25 planted, 40 absent */
            }
            /* And the neighbours, which is what a one-too-far walk of the pool slot reads.
             * probes/pooltail.c measured it: a slot holds eight words, so for a member with n = 8 the
             * entry at [n] is the first word of the next slot -- U+02B9's is U+02BA, which its set
             * does not accept. Fifteen slots are like that, so walking one member too far is a genuine
             * false-match bug, and planting only MEMBERS of the set could never catch it. */
            for (mem = (r > 4 ? r - 4 : 1); mem <= (unsigned)r + 10 && mem < 65536; ++mem) {
                for (k = 0; k < 40; ++k) s[k] = (wchar_t)FILL;
                plant(s, 40, 25, (wchar_t)mem, set);
            }
        }
        printf("  7. every member of every dispatch class (counts 0,2,3,4,5,6,7,8 and the 255\n"
               "     sentinel) as a set member, present and absent: %ld\n", cases - before);
    }

    /* 8. The chunk boundary. The accept list is scanned four members at a time, so a set whose
     * expansion lands exactly on 4, or just over it, exercises the loop's edge. A member with n
     * partners contributes n entries, so sets are built to hit the boundary deliberately.
     */
    {
        long before = cases;
        static wchar_t s[64], set[12];
        int nset;
        for (nset = 1; nset <= 8; ++nset) {
            for (k = 0; k < 40; ++k) s[k] = L'z';
            s[40] = 0;
            for (k = 0; k < nset; ++k) set[k] = (wchar_t)(L'A' + k);
            set[nset] = 0;
            one(s, set);                                 /* no match: the full length */
            for (m = 0; m < nset; ++m) {
                s[30] = (wchar_t)(L'a' + m);             /* the LAST set member matters most */
                one(s, set);
                s[30] = L'z';
            }
            /* and with the match before, at, and after a block boundary */
            for (m = 30; m <= 34; ++m) {
                s[m] = (wchar_t)(L'a' + (nset - 1));
                one(s, set);
                s[m] = L'z';
            }
        }
        printf("  8. the chunk boundary: sets of 1..8 characters, each matched through its LAST\n"
               "     member and across a block boundary: %ld\n", cases - before);
    }

    /* 9. The accept-list cap. Sixteen entries is the cap, and beyond it the whole call takes the
     * scalar path -- so the answer must be identical on both sides of that line. A one-character set
     * member with four partners contributes four entries, so four such members fill the list exactly.
     */
    {
        long before = cases;
        static wchar_t s[64], set[24];
        int nset;
        for (nset = 1; nset <= 12; ++nset) {
            for (k = 0; k < 48; ++k) s[k] = (wchar_t)FILL;
            s[48] = 0;
            for (k = 0; k < nset; ++k) set[k] = (wchar_t)(L'A' + k);
            set[nset] = 0;
            one(s, set);
            for (m = 0; m < nset; ++m) plant(s, 48, 33, (wchar_t)(L'a' + m), set);
            /* a set member with the 255 sentinel forces the scalar path whatever the length */
            set[nset] = SHY; set[nset + 1] = 0;
            plant(s, 48, 7, (wchar_t)CGJ, set);          /* 7: found through the bitmap member */
            set[nset] = 0;
        }
        printf("  9. the accept-list cap: sets of 1..12 characters with four partners each, and the\n"
               "     same sets plus a 255-sentinel member that forces the scalar path: %ld\n",
               cases - before);
    }

    /* 10. a guard page, with the terminator as the last readable code unit */
    {
        long before = cases;
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  10. GUARD PAGE SETUP FAILED\n"); ++failures;
        } else {
            static wchar_t set[4];
            for (k = 1; k <= 60; ++k) {
                wchar_t* s = (wchar_t*)(base + pg) - (k + 1);
                for (j = 0; j < k; ++j) s[j] = (wchar_t)(L'a' + (j % 5));
                s[k] = 0;
                one(s, L"Q");                            /* nothing matches: must stop at the NUL */
                one(s, L"E");
                one(s, L"");
                set[0] = SHY; set[1] = 0;
                one(s, set);                             /* a NUL-matching set member, scalar path */
                s[k - 1] = L'q';
                one(s, L"Q");                            /* the match IS the last character */
                s[k - 1] = (wchar_t)(L'a' + ((k - 1) % 5));
            }
            printf("  10. every length 1..60 with the terminator last before a guard page, with a\n"
                   "      set that matches nothing, one that matches the last character, and a\n"
                   "      255-sentinel set: %ld\n", cases - before);
        }
    }

    /* 11. NON-ZERO memory after the terminator: it must never be read.
     *
     * Every string above lives in a zero-filled buffer, so reading one code unit too far would find a
     * NUL and look correct. Change 284 was caught by exactly this shape in its live gate.
     */
    {
        long before = cases;
        static wchar_t s[64];
        int len;
        for (len = 0; len <= 20; ++len) {
            for (k = 0; k < 64; ++k) s[k] = L'W';        /* every code unit non-zero */
            for (k = 0; k < len; ++k) s[k] = (wchar_t)(L'a' + (k % 5));
            s[len] = 0;
            one(s, L"W");                                /* 'W' exists only PAST the terminator */
            one(s, L"Q");
            one(s, L"");
            one(s, L"A");
        }
        printf("  11. a non-zero buffer, with a set matching only what lies PAST the terminator: "
               "%ld\n", cases - before);
    }

    /* 12. a long string, and a count that does not fit in 16 bits */
    {
        long before = cases;
        static wchar_t big[70000];
        for (k = 0; k < 66000; ++k) big[k] = L'a';
        big[66000] = 0;
        one(big, L"z");                                  /* 66000 */
        one(big, L"");                                   /* 66000 */
        big[65000] = L'q';
        one(big, L"Q");                                  /* 65000, past 16 bits */
        big[65000] = L'a';
        big[40000] = L'q';
        one(big, L"Q");
        big[40000] = L'a';
        one(big, L"A");                                  /* 0 */
        printf("  12. a 66000-character string: counts past 16 bits, and the empty set: %ld\n",
               cases - before);
    }

    /* 13. An empty set, over every code unit.
     *
     * An empty set expands to nothing, and the implementation appends a single zero entry rather than
     * special-casing it. A mutant that drops that append broadcasts whatever the uninitialised stack
     * slot happens to hold -- and it SURVIVED both gates, because that garbage never occurred in any
     * test string. Which garbage it is cannot be predicted, so the corpus stops depending on it:
     * every code unit from 1 to 65535 appears in a string that is asked with an empty set, and the
     * answer must always be the string's length.
     */
    {
        long before = cases;
        static wchar_t s[80];
        unsigned c;
        for (c = 1; c < 65536; c += 64) {
            int q;
            for (q = 0; q < 64; ++q) {
                unsigned u = c + (unsigned)q;
                s[q] = (wchar_t)(u < 65536 ? u : 1);
            }
            s[64] = 0;
            one(s, L"");                                 /* always 64, whatever the stack held */
        }
        printf("  13. an empty set over strings covering EVERY code unit 1..65535: %ld\n",
               cases - before);
    }

    /* 14. a long string with a multi-chunk set, so the answer lies beyond the first window.
     *
     * The scan divides the string into windows of 4, 8, 16, ... blocks and the count is measured from
     * the string base, not from the current window. A mutant that measured it from the window survived
     * gate 1 entirely: every windowed corpus string above is shorter than 64 code units, so the first
     * window covers all of them and the two are the same address. The only long string in the corpus
     * used a one-character set, which takes the single-pass shortcut and never opens a window at all.
     */
    {
        long before = cases;
        static wchar_t s[600], set[24];
        for (k = 0; k < 20; ++k) set[k] = (wchar_t)(L'M' + k);
        set[20] = 0;
        /* The filler must not be in the set, and the first version of this corpus got that wrong: it
         * filled with 'z', and the set runs U+004D..U+0060 which includes 'Z' -- so case-insensitively
         * every single character of the string was already a match, every answer was 0, and every
         * answer therefore lay in the FIRST window. probes/windiag.c printed it: ours 0, live 0, for
         * every planted position. The mutant this corpus was written to catch survived it untouched.
         * '0' is in none of the twenty members' sets. */
        for (m = 0; m < 600; ++m) s[m] = L'0';
        s[520] = 0;
        one(s, set);                                     /* no match: 520, many windows along */
        for (m = 0; m < 520; m += 7) {
            s[m] = (wchar_t)(L'm' + (m % 20));
            one(s, set);                                 /* the answer at m, in whichever window */
            s[m] = L'0';
        }
        printf("  14. a 520-character string with a 20-character set, the answer swept across every\n"
               "      window: %ld\n", cases - before);
    }

    /* 15. Sentinel sets whose bitmap does not contain a NUL.
     *
     * probes/pooltail.c found eleven distinct 255-sentinel sets, and ten of them do not accept a NUL --
     * only the 3238-member ignorable set does. Every sentinel case in the corpus above used that one,
     * so the scalar bitmap loop's own terminator test was never needed: the bitmap stopped the loop for
     * free. A mutant removing that test therefore survived both gates. With one of the other ten, a
     * missing terminator test runs straight off the end of the string.
     */
    {
        long before = cases;
        static const unsigned short sent[] = { 0x030D, 0x0591, 0x1CD0, 0x27F8, 0x3005,
                                               0xB9C6, 0xC0AA, 0xC542, 0xC78E, 0xD7A2 };
        static wchar_t s[200], set[4];
        int i, len;
        for (i = 0; i < 10; ++i) {
            set[0] = (wchar_t)sent[i]; set[1] = 0;
            for (len = 1; len <= 100; len += 9) {
                for (k = 0; k < len; ++k) s[k] = (wchar_t)(L'a' + (k % 5));
                s[len] = 0;
                one(s, set);                             /* nothing matches: the terminator must stop it */
            }
            /* not vacuous: a real member of that same set, planted */
            for (k = 0; k < 60; ++k) s[k] = (wchar_t)(L'a' + (k % 5));
            s[60] = 0;
            s[33] = (wchar_t)sent[i];
            one(s, set);
            /* and mixed with an ordinary member, so all three scalar loops run in one call */
            set[1] = L'Q'; set[2] = 0;
            one(s, set);
            set[1] = 0;
        }
        /* the same against a guard page: running past the terminator is then a fault, not a wrong
           answer, which is the strongest form this case can take */
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  15. GUARD PAGE SETUP FAILED\n"); ++failures;
        } else {
            for (i = 0; i < 10; ++i) {
                set[0] = (wchar_t)sent[i]; set[1] = 0;
                for (len = 1; len <= 40; ++len) {
                    wchar_t* g = (wchar_t*)(base + pg) - (len + 1);
                    for (k = 0; k < len; ++k) g[k] = (wchar_t)(L'a' + (k % 5));
                    g[len] = 0;
                    one(g, set);
                }
            }
        }
        printf("  15. the ten sentinel sets that do NOT accept a NUL, with nothing matching, with a\n"
               "      real member planted, mixed with an ordinary member, and against a guard page: "
               "%ld\n", cases - before);
    }

    /* 16. The scalar pool loop at the edge of a slot.
     *
     * A pool slot is eight words wide, so for a set member with EIGHT partners the entry just past its
     * members is the first word of the next slot -- probes/pooltail.c measured fifteen code units for
     * which that neighbour is NOT accepted by the set, U+02B9's being U+02BA. A mutant walking one
     * member too far is therefore a genuine false-match bug.
     *
     * Corpus 7 plants every member of every dispatch class, and corpus 9 mixes a sentinel member in to
     * force the scalar path -- but never both at once, so the scalar POOL loop never ran with a
     * high-count member. A set of {a member with 5..8 partners, a sentinel} does exactly that, and the
     * string sweeps the member's neighbourhood so the out-of-slot value is among the characters tried.
     */
    {
        long before = cases;
        static const unsigned short highcount[] = { 0x004B, 0x00C6, 0x0598, 0x02B9 };
        static wchar_t s[64], set[4];
        unsigned mem;
        int i;
        for (i = 0; i < 4; ++i) {
            set[0] = (wchar_t)highcount[i];
            set[1] = SHY;                       /* the sentinel forces the call-free scalar path */
            set[2] = 0;
            for (mem = (unsigned)highcount[i] - 4; mem <= (unsigned)highcount[i] + 12; ++mem) {
                if (mem < 1 || mem > 65535) continue;
                for (k = 0; k < 40; ++k) s[k] = (wchar_t)FILL;
                plant(s, 40, 21, (wchar_t)mem, set);
            }
            /* and with the sentinel member first, so the loop order is exercised both ways */
            set[0] = SHY;
            set[1] = (wchar_t)highcount[i];
            for (mem = (unsigned)highcount[i] - 2; mem <= (unsigned)highcount[i] + 10; ++mem) {
                if (mem < 1 || mem > 65535) continue;
                for (k = 0; k < 40; ++k) s[k] = (wchar_t)FILL;
                plant(s, 40, 21, (wchar_t)mem, set);
            }
        }
        printf("  16. the scalar POOL loop at a slot edge: a 5..8-partner member together with a\n"
               "      sentinel, the string sweeping the member's neighbourhood: %ld\n",
               cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d   (live stopped early %ld, ran to the end %ld)\n",
           cases, failures, n_early, n_full);
    if (n_early < 500 || n_full < 500) {
        printf("  the corpus did not reach both outcomes in volume\n");
        ++failures;
    }
    if (!failures)
        printf("CORRECTNESS: PASS (the returned COUNT exact vs live shlwapi and vs the scalar model,\n"
               "over every alignment, matches below the string pointer, every member of every\n"
               "dispatch class, the chunk and accept-list boundaries, the scalar path, embedded NULs,\n"
               "a guard page and a non-zero buffer)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
