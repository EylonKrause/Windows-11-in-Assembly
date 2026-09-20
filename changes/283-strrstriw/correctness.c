/* changes/283-strrstriw/correctness.c
 *
 * Gate 1 for shlwapi!StrRStrIW: Ours vs the scalar model vs the live export, on the returned
 * pointer compared as a BYTE offset -- because change 282 found a mutant that returned a pointer
 * one byte into the middle of a wchar_t and survived both gates, since `p - base` on a wchar_t*
 * divides the odd byte away.
 *
 * The corpus is built where a backward substring search goes wrong:
 *
 *   * The last match, with the same needle present many times. a forward search returning the
 *     first hit passes any test with one occurrence in it.
 *   * `end` Bounds the start, not the match. probes/bounds.c measured that over "abcXYZabc" the
 *     answer becomes 6 as soon as end reaches start+7, even though that match runs to index 8. So
 *     every end position from start to start+len is swept for every haystack.
 *   * The terminator wins over `end`. a NUL at index 4 hides a match at 9 even when end is far
 *     past it, so NULs are planted at every position.
 *   * Needles of every length from 1 up, including longer than the haystack.
 *   * The intransitive triple inside a substring, because that is what makes the relation not an
 *     equivalence: "x<D7A2>y" matches both "x<D7B0>y" and "x<D7B1>y" while those two do not match
 *     each other.
 *   * Ignorable characters, because a collation-based search would skip them and this one must
 *     match them: "ab<SOFT HYPHEN>cd" does NOT contain "abc".
 *   * And a guard page, with the terminator as the last readable code unit -- the export reads to
 *     the terminator regardless of `end`, so that is where the scan must stop.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef PCWSTR (WINAPI *F3)(PCWSTR, PCWSTR, PCWSTR);

extern const wchar_t* wia_strrstriw(const wchar_t*, const wchar_t*, const wchar_t*);
const wchar_t* ref_strrstriw(const wchar_t*, const wchar_t*, const wchar_t*);
int wia_sci_init(void);
int wia_sci_match(unsigned needle, unsigned w);
unsigned wia_sci_partner(unsigned c);

static F3 sys;
static int failures = 0;
static long cases = 0, n_hit = 0, n_miss = 0;

/* BYTE offsets: change 282's lesson, where a one-byte error was invisible in code-unit offsets */
static long off(const wchar_t* base, const wchar_t* p)
{ return p ? (long)((const char*)p - (const char*)base) : -1; }

static void one(const wchar_t* s, const wchar_t* e, const wchar_t* need)
{
    const wchar_t *ra, *rb, *rc;
    ra = wia_strrstriw(s, e, need);
    rb = sys(s, e, need);
    rc = ref_strrstriw(s, e, need);
    ++cases;
    if (rb) ++n_hit; else ++n_miss;
    if (off(s, ra) != off(s, rb) || off(s, ra) != off(s, rc)) {
        if (failures < 12)
            printf("  FAIL nlen=%d end=+%d: ours %ld  live %ld  model %ld  (BYTE offsets)\n",
                   (int)wcslen(need), (int)(e - s), off(s, ra), off(s, rb), off(s, rc));
        ++failures;
    }
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    SYSTEM_INFO si;
    unsigned char* base;
    SIZE_T pg;
    int k, j, m;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F3)GetProcAddress(hs, "StrRStrIW");
    if (!sys) { printf("no StrRStrIW\n"); return 2; }
    printf("== CORRECTNESS: shlwapi!StrRStrIW ==\n");
    if (wia_sci_init()) { printf("  change 281's tables disagree with the live export\n"); return 1; }
    printf("  0. change 281's match relation rebuilt and re-checked against the live export\n");

    /* 1. repeated needles: the LAST must win, and every `end` must move the answer correctly */
    {
        long before = cases;
        static wchar_t h[81];
        static wchar_t n[8];
        for (k = 0; k < 80; ++k) h[k] = (wchar_t)(L'a' + (k % 4));
        h[80] = 0;
        for (j = 1; j <= 6; ++j) {
            for (m = 0; m < j; ++m) n[m] = (wchar_t)(L'A' + (m % 4));
            n[j] = 0;
            for (m = 0; m <= 80; ++m) one(h, h + m, n);
        }
        printf("  1. a 4-periodic haystack, needles of length 1..6, EVERY end 0..80: %ld\n",
               cases - before);
    }

    /* 2. every alignment and every needle length, with the only match at every position */
    {
        long before = cases;
        static wchar_t pad[160];
        static wchar_t n[6];
        for (k = 0; k < 32; ++k) {
            wchar_t* h = pad + 32 + k;
            for (j = 2; j <= 4; ++j) {
                for (m = 0; m < j; ++m) n[m] = (wchar_t)(L'X' + m);
                n[j] = 0;
                for (m = 0; m + j <= 40; ++m) {
                    int q;
                    for (q = 0; q < 40; ++q) h[q] = L'z';
                    h[40] = 0;
                    for (q = 0; q < j; ++q) h[m + q] = (wchar_t)(L'x' + q);
                    one(h, h + 40, n);
                    one(h, h + m + 1, n);          /* end exactly past the match start */
                    one(h, h + m, n);              /* end exactly AT the match start: excluded */
                }
            }
        }
        printf("  2. every alignment x needle length 2..4 x match position, with `end` at, just\n"
               "     past, and far past the match: %ld\n", cases - before);
    }

    /* 3. the terminator beats `end` */
    {
        long before = cases;
        static wchar_t h[24];
        for (k = 0; k < 20; ++k) h[k] = (wchar_t)(L'a' + k);
        h[20] = 0;
        for (k = 1; k < 20; ++k) {
            wchar_t save = h[k];
            h[k] = 0;
            one(h, h + 20, L"st");                 /* 's','t' are at 18,19 -- behind the NUL */
            one(h, h + 20, L"ab");                 /* in front of it */
            one(h, h + 20, L"cd");
            h[k] = save;
        }
        printf("  3. a NUL at every position, with `end` far past it: %ld\n", cases - before);
    }

    /* 4. ignorables must be MATCHED, not skipped, and the intransitive triple must hold */
    {
        long before = cases;
        static wchar_t h1[] = { L'a', L'b', 0x00AD, L'c', L'd', 0 };
        static wchar_t h2[] = { L'x', 0xD7A2, L'y', 0 };
        static wchar_t h3[] = { L'x', 0xD7B0, L'y', 0 };
        static wchar_t n1[] = { L'x', 0xD7B0, L'y', 0 };
        static wchar_t n2[] = { L'x', 0xD7B1, L'y', 0 };
        static wchar_t n3[] = { L'a', L'b', 0x200B, L'c', 0 };
        static wchar_t n4[] = { L'a', L'b', 0x034F, L'c', 0 };
        one(h1, h1 + 5, L"abc");                   /* must NOT be found: no span collation */
        one(h1, h1 + 5, L"ab");
        one(h1, h1 + 5, L"cd");
        one(h1, h1 + 5, n3);                       /* 0x200B matches only ITSELF: not a
                                                      match, and change 285 measured why */
        one(h1, h1 + 5, n4);                       /* 0x034F IS a partner of 0x00AD */
        one(h2, h2 + 3, n1);
        one(h2, h2 + 3, n2);
        one(h3, h3 + 3, n2);                       /* the third leg: must NOT match */
        printf("  4. ignorables matched not skipped, and the intransitive triple: %ld\n",
               cases - before);
    }

    /* 5. degenerate arguments */
    {
        long before = cases;
        static wchar_t t[] = L"abcXYZabc";
        one(t, t + 9, L"");
        one(t, t, L"abc");
        one(t, t + 2, L"abc");
        one(t, t + 9, L"abcXYZabcQ");              /* longer than the haystack */
        one(t, t + 9, L"Q");
        {
            const wchar_t* r1 = wia_strrstriw(0, 0, L"a");
            const wchar_t* r2 = sys(0, 0, L"a");
            const wchar_t* r3 = ref_strrstriw(0, 0, L"a");
            ++cases;
            if (r1 || r2 || r3) { printf("  FAIL: NULL did not return NULL\n"); ++failures; }
        }
        {
            const wchar_t* r1 = wia_strrstriw(t, t + 9, 0);
            const wchar_t* r2 = sys(t, t + 9, 0);
            const wchar_t* r3 = ref_strrstriw(t, t + 9, 0);
            ++cases;
            if (r1 || r2 || r3) { printf("  FAIL: a NULL needle did not return NULL\n"); ++failures; }
        }
        printf("  5. empty needle, empty range, over-long needle, NULL arguments: %ld\n",
               cases - before);
    }

    /* 6. a guard page, with the terminator as the last readable code unit */
    {
        long before = cases;
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  6. GUARD PAGE SETUP FAILED\n"); ++failures;
        } else {
            for (k = 1; k <= 60; ++k) {
                wchar_t* h = (wchar_t*)(base + pg) - (k + 1);
                for (j = 0; j < k; ++j) h[j] = (wchar_t)(L'a' + (j % 5));
                h[k] = 0;
                one(h, h + k, L"AB");
                one(h, h + k, L"E");
                one(h, h + k, L"#");
                one(h, h + k + 8, L"E");           /* an `end` past the terminator */
            }
            printf("  6. every length 1..60 with the terminator last before a guard page, and\n"
                   "     `end` past it: %ld\n", cases - before);
        }
    }

    /* 7. Every interior character must be verified.
     *
     * This corpus exists because the corpora above could not express its case. A verifier that
     * checks the first character, the last character, and then every SECOND one in between agreed
     * with the live export on 11740 of 11741 cases. Corpus 1 is 4-periodic and its needles are
     * prefixes of that period, so checking a subset of positions implies the whole match. Corpus 2
     * fills the haystack with 'z' and gives the needle a first character that occurs at exactly one
     * planted site, so the only candidate is already a full match. The single case that did catch
     * it was corpus 4's intransitive triple -- the one needle in the whole corpus whose first and
     * last characters match while its middle does not -- and that was an accident of a test written
     * for an entirely different purpose.
     *
     * So the near-miss is now built on purpose: for every needle length and every interior index, a
     * haystack holding the needle with exactly that one interior character changed, padded with a
     * character that matches the needle's first one so the vector filter rejects nothing and the
     * verifier is the only defence. Each near-miss is paired with its repaired control, so the case
     * proves the verifier rejects for the right reason rather than rejecting always.
     */
    {
        long before = cases;
        static wchar_t h[64];
        static wchar_t n[10];
        int p, pad;

        for (pad = 0; pad < 2; ++pad) {
            for (j = 3; j <= 7; ++j) {
                for (m = 0; m < j; ++m) n[m] = (wchar_t)(L'a' + m);
                n[j] = 0;
                for (p = 1; p <= j - 2; ++p) {
                    /* pad 0: every position is a first-character candidate.
                       pad 1: every position matches the LAST character too, so the
                       last-character reject cannot do the work either. */
                    for (k = 0; k < 48; ++k) h[k] = pad ? n[j - 1] : n[0];
                    h[48] = 0;
                    for (k = 0; k < j; ++k) h[20 + k] = n[k];
                    h[20 + p] = (wchar_t)(L'a' + j + 1);   /* a letter that cannot match n[p] */
                    one(h, h + 48, n);                     /* the near-miss: NOT a match */
                    one(h, h + 30, n);
                    one(h, h + 21, n);
                    h[20 + p] = (wchar_t)(n[p] - 32);      /* the control, in the other case */
                    one(h, h + 48, n);                     /* must be found at 20 */
                    one(h, h + 22, n);
                }
            }
        }

        /* The same family on the WIDE path: a first character with more than four partners
         * bypasses the vector filter entirely, so a different dispatch verifies these.
         *
         * The filler is 0x034F, not 0x200B, and that was a real mistake. This block was first written
         * with a 0x200B filler and a comment claiming that it and 0x00AD "are both ignorable and match
         * each other, so every position is a candidate". Change 285's relation probe measured the
         * truth: n[0x200B] is 0 -- the zero width space matches only itself and is not one of the 3237
         * ignorables at all, while match(0x00AD, 0x034F) is 1. So the filler matched nothing, NO
         * position was a candidate, and this family was quietly testing the empty case while its
         * comment claimed the opposite. The cases still passed, because all three sides agreed on the
         * answer -- which is exactly what makes a test that measures nothing hard to notice. */
        for (j = 3; j <= 6; ++j) {
            n[0] = 0x00AD;
            for (m = 1; m < j; ++m) n[m] = (wchar_t)(L'a' + m);
            n[j] = 0;
            for (p = 1; p <= j - 2; ++p) {
                for (k = 0; k < 48; ++k) h[k] = 0x034F;
                h[48] = 0;
                for (k = 0; k < j; ++k) h[20 + k] = n[k];
                h[20 + p] = (wchar_t)(L'a' + j + 1);
                one(h, h + 48, n);
                one(h, h + 30, n);
                h[20 + p] = n[p];
                one(h, h + 48, n);
            }
        }

        printf("  7. every needle length x EVERY interior index, as a one-character near-miss\n"
               "     with the filter neutralised, plus its repaired control, on both the normal\n"
               "     and the WIDE dispatch: %ld\n", cases - before);
    }

    /* 8. The needle-length clamp, and the empty needle, each made observable.
     *
     * Two mutants survived everything above and were caught only by the live-substitution gate,
     * which means this corpus could not express either case:
     *
     *   - Dropping the clamp `hlen -= nlen`. The highest candidate start is
     *     min(start + hlen - nlen, end - 1). Without the clamp it becomes min(start + hlen, end - 1),
     *     and in every corpus above `end - 1` was the smaller of the two, so the clamp never decided
     *     anything and its removal changed no answer. To make it decide, the needle must be able to
     *     match ACROSS the terminator: change 282 established that 3238 code units match a NUL, so
     *     a needle of 'q' followed by 0x00AD (one of them) placed where the only 'q' is the last
     *     character of the string matches at hlen-1 if and only if the candidate was never clamped.
     *     The live export and the model both stop at the terminator and answer "not found".
     *
     *   - Dropping the empty-needle refusal. Corpus 5 asks for an empty needle exactly once, with
     *     `end` inside the string, and the unguarded code happens to agree there: it looks for the
     *     needle's first code unit, which is the terminator, finds it above the candidate cap, and
     *     returns NULL for the wrong reason. Asked with `end` AT and PAST the terminator it does
     *     not. One case of a degenerate input is not coverage of it.
     */
    {
        long before = cases;
        static wchar_t h[64];
        static wchar_t n[8];
        int hlen = 40;

        /* (a) the clamp: the only 'q' is the last character, and the needle's tail matches NUL */
        for (j = 2; j <= 4; ++j) {
            for (k = 0; k < 64; ++k) h[k] = 0;
            for (k = 0; k < hlen; ++k) h[k] = L'z';
            h[hlen - 1] = L'q';
            h[hlen] = 0;
            n[0] = L'Q';
            for (m = 1; m < j; ++m) n[m] = 0x00AD;         /* every one of these matches a NUL */
            n[j] = 0;
            one(h, h + hlen, n);                           /* not found: the match needs the NUL */
            one(h, h + hlen + 1, n);
            one(h, h + hlen + 8, n);                       /* `end` well past the terminator */
            one(h, h + hlen - 1, n);
            /* the control: give the needle a real second character that IS there, so the same
               shape must now be FOUND, proving the refusal above is not simply unconditional */
            h[hlen - 1] = L'q';
            h[hlen - 2] = L'p';
            n[0] = L'P';
            n[1] = L'Q';
            n[2] = 0;
            one(h, h + hlen, n);                           /* found at hlen-2 */
            one(h, h + hlen + 8, n);
        }

        /* (b) the empty needle at every interesting `end`, including past the terminator, and
               with an embedded NUL in front of it */
        for (k = 0; k < 64; ++k) h[k] = 0;
        for (k = 0; k < hlen; ++k) h[k] = (wchar_t)(L'a' + (k % 7));
        h[hlen] = 0;
        for (m = 0; m <= hlen + 8; ++m) one(h, h + m, L"");
        h[13] = 0;
        for (m = 0; m <= hlen + 8; m += 7) one(h, h + m, L"");

        /* (c) The empty needle over a haystack that contains a nul-matching code unit.
         *
         * Part (b) above still did not catch the mutant that drops the empty-needle refusal, which
         * is how it was found that (b) agrees for the wrong reason a second time: with the refusal
         * gone the search keys on the needle's first code unit, which for an empty needle is the
         * TERMINATOR, and then looks for a haystack character matching a NUL. Over a haystack of
         * plain letters there is no such character, so it finds nothing and returns NULL -- the
         * right answer, reached by a route that proves nothing. Put a soft hyphen in the haystack
         * and the unguarded code reports a match there while the live export still returns NULL.
         */
        for (k = 0; k < 64; ++k) h[k] = 0;
        for (k = 0; k < hlen; ++k) h[k] = (wchar_t)(L'a' + (k % 7));
        h[9] = 0x00AD;
        h[27] = 0x00AD;
        h[hlen] = 0;
        for (m = 0; m <= hlen + 8; ++m) one(h, h + m, L"");
        one(h, h + hlen, L"a");                        /* the same haystack still works normally */
        one(h, h + hlen, L"g");

        printf("  8. the needle-length clamp made observable by a needle whose tail matches NUL,\n"
               "     with its found-control, and the empty needle at every `end` including past\n"
               "     the terminator: %ld\n", cases - before);
    }

    /* 9. a match planted below `start`, at every alignment.
     *
     * Dropping the bottom edge mask in the backward block scan -- the `and eax, edx` that clears the
     * bits for code units lying below `start` -- SURVIVED everything above. The scan reads aligned
     * 32-byte blocks, so the block containing `start` almost always extends below it, and the mask
     * is the only thing stopping a hit there from being accepted. Every corpus above begins its
     * haystack far from any planted needle, so there was never anything below `start` to find and
     * the mask never had to do its job.
     *
     * Here the needle is planted BELOW `start` and nowhere at or above it, at all 32 alignments of
     * `start` within the block, so the answer must be "not found" and any leaked bit becomes a
     * pointer below the string the caller gave.
     */
    {
        long before = cases;
        static wchar_t buf[128];
        int a;
        for (a = 0; a < 32; ++a) {
            wchar_t* st = buf + 32 + a;
            for (k = 0; k < 128; ++k) buf[k] = L'z';
            buf[10] = L'x'; buf[11] = L'y';        /* below `start` for every alignment */
            buf[20] = L'x'; buf[21] = L'y';
            buf[30] = L'x'; buf[31] = L'y';        /* immediately below the lowest `start` */
            st[30] = 0;
            one(st, st + 30, L"XY");               /* not found: every match is below `start` */
            one(st, st + 1, L"XY");
            one(st, st + 2, L"XY");
            st[5] = L'x'; st[6] = L'y';            /* the control, at `start` + 5 */
            one(st, st + 30, L"XY");
            one(st, st + 6, L"XY");
        }
        printf("  9. a match planted BELOW `start` at all 32 alignments, with its found-control\n"
               "     above `start`: %ld\n", cases - before);
    }

    /* 10. Every dispatch class of the first-character filter, and every member of its set.
     *
     * The filter dispatches on how many code units the needle's first character matches: none takes
     * a single-broadcast path, up to four takes a four-register path, and more takes a WIDE path
     * that skips the vector filter. Moving that threshold from 4 to 200 SURVIVED the whole corpus,
     * because the only many-partner needle used anywhere above was an ignorable, and change 281
     * stores the 3320 ignorables behind a count of 255 -- so they took the WIDE path either way and
     * the moved threshold changed nothing.
     *
     * probes/partners.c measured which counts actually occur: 0, 2, 3, 4, 5, 6, 7, 8 and the 255
     * sentinel, nine in all. The representatives below drive one needle per class, and the haystack
     * is planted with every member of the set in turn -- because a four-register path asked to hold
     * a five-member set must drop a member, and only the dropped one exposes it.
     */
    {
        long before = cases;
        static const unsigned short reps[] = { 0x0001, 0x0020, 0x0023, 0x0035,
                                               0x004B, 0x00C6, 0x0598, 0x02B9, 0x00AD };
        static wchar_t h[64];
        static wchar_t n[8];
        unsigned r, mem;
        int i, seen;
        for (i = 0; i < 9; ++i) {
            r = reps[i];
            seen = 0;
            for (mem = 1; mem < 65536; ++mem) {
                if (!wia_sci_match(r, mem)) continue;
                ++seen;
                /* the ignorable set has 3320 members; sample it rather than plant all of them */
                if (r == 0x00AD && (seen % 101) != 1) continue;
                for (k = 0; k < 64; ++k) h[k] = 0;
                for (k = 0; k < 40; ++k) h[k] = 0xFFFD;   /* filler that matches only itself */
                h[25] = (wchar_t)mem; h[26] = L'm'; h[27] = L'n';
                h[40] = 0;
                n[0] = (wchar_t)r; n[1] = L'M'; n[2] = L'N'; n[3] = 0;
                one(h, h + 40, n);                        /* found at 25 whichever member it is */
                one(h, h + 26, n);
                one(h, h + 25, n);                        /* `end` excludes the match start */
                h[26] = 0xFFFD;                           /* an interior near-miss on this path */
                one(h, h + 40, n);
            }
        }
        printf("  10. one needle per filter dispatch class (partner counts 0,2,3,4,5,6,7,8 and the\n"
               "      255 sentinel), planted with every member of the set in turn: %ld\n",
               cases - before);
    }

    /* 11. a guard page where candidates fail and the scan must continue.
     *
     * Corpus 6 puts the terminator as the last readable code unit, but its needles match on the
     * first try, so the scan never has to resume after a rejected candidate. A mutant that stopped
     * re-arming the filter after a failed verification therefore survived: it only misbehaves once a
     * candidate has actually been rejected, and then it faults reading past the page.
     *
     * So here the needle's FIRST character occurs at almost every position while the whole needle
     * matches low down or not at all -- forcing a long run of rejected candidates that starts right
     * next to the unreadable page.
     */
    {
        long before = cases;
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  11. GUARD PAGE SETUP FAILED\n"); ++failures;
        } else {
            for (k = 4; k <= 60; ++k) {
                wchar_t* h = (wchar_t*)(base + pg) - (k + 1);
                for (j = 0; j < k; ++j) h[j] = L'a';
                h[k] = 0;
                h[3] = L'q';                        /* the only 'q': "AQ" matches at 2 alone */
                one(h, h + k, L"AQ");               /* every other candidate is rejected */
                one(h, h + k, L"AZ");               /* rejected everywhere, up to the page */
                one(h, h + k, L"AAZ");
                one(h, h + k + 8, L"AZ");           /* with `end` past the terminator too */
            }
            printf("  11. a guard page with the needle's first character at nearly every position\n"
                   "      and the match low or absent, so candidates are rejected up to the page\n"
                   "      boundary: %ld\n", cases - before);
        }
    }

    /* 12. Non-zero memory after the terminator: The virtual NUL, proved rather than assumed.
     *
     * Every haystack above lives in a static, zero-filled array, so the code units after a
     * terminator are genuinely NUL -- and that makes two completely different rules indistinguishable:
     *
     *   (A) the export READS the memory after the terminator and compares it;
     *   (B) the export treats the string as ending there and compares the needle's remaining
     *       characters against a VIRTUAL NUL, never loading them.
     *
     * Corpus 8 was written believing (A) was irrelevant and the whole match simply could not cross
     * the terminator; that was wrong. Then the implementation was rebuilt on (A), and the
     * live-substitution gate -- which reuses ONE buffer across 30000 cases, so the code units after
     * a terminator hold the previous case's letters -- found three disagreements at once. Only then
     * did probes/pastnul2.c settle it as (B).
     *
     * So the deciding shape belongs in this corpus too: a buffer filled with a NON-ZERO character,
     * a string written into it, and needles whose tails either match a NUL (must be found, under
     * (B), though the real memory there is not a NUL) or match that filler character (must NOT be
     * found, though the real memory there IS that character).
     */
    {
        long before = cases;
        static wchar_t h[64];
        static wchar_t n[40];
        int p2, len;

        for (len = 1; len <= 12; ++len) {
            for (k = 0; k < 64; ++k) h[k] = L'W';          /* every code unit non-zero */
            for (k = 0; k < len; ++k) h[k] = (wchar_t)(L'a' + (k % 5));
            h[len] = 0;                                    /* and 'W' from here on */

            /* a needle ending in NUL-matching characters, anchored on the string's last character */
            for (p2 = 1; p2 <= 4; ++p2) {
                n[0] = (wchar_t)((L'a' + ((len - 1) % 5)) - 32);   /* the last character, upper */
                for (k = 1; k <= p2; ++k) n[k] = 0x00AD;
                n[p2 + 1] = 0;
                one(h, h + 64, n);                         /* found under (B), not under (A) */
                one(h, h + len, n);
                one(h, h + len - 1, n);                    /* `end` excludes the only candidate */
            }

            /* the mirror: a needle whose tail is the FILLER, which really is there. Under (B) it
               must NOT be found, because past the end everything compares as a NUL. */
            for (p2 = 1; p2 <= 3; ++p2) {
                n[0] = (wchar_t)((L'a' + ((len - 1) % 5)) - 32);
                for (k = 1; k <= p2; ++k) n[k] = L'W';
                n[p2 + 1] = 0;
                one(h, h + 64, n);
                one(h, h + len, n);
            }

            /* and mixed: one NUL-matching character then the filler */
            n[0] = (wchar_t)((L'a' + ((len - 1) % 5)) - 32);
            n[1] = 0x00AD;
            n[2] = L'W';
            n[3] = 0;
            one(h, h + 64, n);
        }

        /* the same question at an EMBEDDED NUL, with real characters behind it */
        for (k = 0; k < 64; ++k) h[k] = L'W';
        h[0] = L'a'; h[1] = L'b'; h[2] = 0; h[3] = L'c'; h[4] = L'd'; h[5] = 0;
        {
            static wchar_t m1[] = { L'B', 0x00AD, 0x00AD, 0 };
            static wchar_t m2[] = { L'B', 0x00AD, L'C', 0 };
            static wchar_t m3[] = { L'B', 0x00AD, L'W', 0 };
            one(h, h + 64, m1);            /* found: the run past the embedded NUL is virtual */
            one(h, h + 64, m2);            /* not found: it never reads the real 'c' */
            one(h, h + 64, m3);
        }

        printf("  12. a haystack in a NON-ZERO buffer, with needle tails that match a NUL (found)\n"
               "      and tails that match the real filler (not found), at the terminator and at an\n"
               "      embedded NUL: %ld\n", cases - before);
    }

    /* 13. a needle whose first character matches a NUL, with `end` past the terminator.
     *
     * A match may start only at a REAL character: the highest candidate is hlen-1, never hlen. The
     * mutant that caps at hlen instead -- letting a match start AT the terminator -- survived
     * everything above, because that only becomes visible when the needle's FIRST character matches a
     * NUL and `end` reaches past the terminator, and no corpus above combines the two. Every needle
     * with a NUL-matching character had it in the TAIL (corpus 8) or was an ignorable in the WIDE
     * family whose `end` stopped inside the string (corpus 7).
     *
     * 0x00AD matches a NUL, so a needle of soft hyphens must find nothing in a string of letters
     * however far `end` reaches -- and must still find a real soft hyphen when one is planted.
     */
    {
        long before = cases;
        static wchar_t h[64];
        static wchar_t n[8];
        int len, e;

        for (len = 1; len <= 10; ++len) {
            for (k = 0; k < 64; ++k) h[k] = L'W';        /* a non-zero buffer again */
            for (k = 0; k < len; ++k) h[k] = (wchar_t)(L'a' + (k % 5));
            h[len] = 0;

            for (j = 1; j <= 3; ++j) {
                for (k = 0; k < j; ++k) n[k] = 0x00AD;
                n[j] = 0;
                /* `end` at, just past, and well past the terminator: never a match, because the
                   terminator is not a candidate position */
                for (e = len; e <= len + 6; ++e) one(h, h + e, n);
            }

            /* and the control: plant a REAL soft hyphen, which must then be found -- at the highest
               such position, not at the terminator beyond it */
            if (len >= 3) {
                h[1] = 0x00AD;
                h[len - 1] = 0x00AD;
                n[0] = 0x00AD; n[1] = 0;
                for (e = len; e <= len + 6; ++e) one(h, h + e, n);
                n[0] = 0x00AD; n[1] = 0x00AD; n[2] = 0;
                for (e = len; e <= len + 6; ++e) one(h, h + e, n);
            }
        }

        printf("  13. a needle whose FIRST character matches a NUL, with `end` at and past the\n"
               "      terminator, plus a planted real one as the control: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d   (live found %ld, missed %ld)\n",
           cases, failures, n_hit, n_miss);
    if (n_hit < 2000 || n_miss < 2000) {
        printf("  the corpus did not reach both outcomes in volume\n");
        ++failures;
    }
    if (!failures)
        printf("CORRECTNESS: PASS (the returned BYTE offset exact vs live shlwapi and vs the\n"
               "scalar model, over repeated matches, every `end`, embedded NULs, ignorables, the\n"
               "intransitive triple and a guard page)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
