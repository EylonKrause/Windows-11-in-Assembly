/* changes/284-strstriw/correctness.c
 *
 * Three-way: ours vs an independent scalar model vs the LIVE shlwapi export.
 *
 * The corpora are inherited from change 283 On purpose. Every one of that change's corpora 7-13
 * exists because a mutant survived, and one of them caught a defect in the shipped implementation
 * rather than in a mutant. Rebuilding this gate from the "obvious" cases would walk into the same
 * holes, so the shapes come over from the start and are adapted to a FORWARD, first-match search:
 *
 *   * interior near-misses at every index, with the vector filter neutralised;
 *   * a match planted where the scan must NOT find it, for a forward scan, below the haystack
 *     pointer and below the resumed scan bound, which is what the bottom edge mask is for;
 *   * every member of every filter dispatch class, because a four-register filter asked to hold a
 *     five-member set must drop one and only the dropped member exposes it;
 *   * candidates that FAIL and then continue, right up against an unreadable page;
 *   * a NON-ZERO buffer, so the virtual NUL is proved rather than assumed;
 *   * a needle whose FIRST character matches a NUL, so "the terminator is not a candidate" is tested;
 *   * and FIRST-match discipline: two matches planted, the lower one required.
 *
 * Offsets are compared in BYTES, never in code units: change 282 found a mutant that returned a
 * pointer one byte into the middle of a wchar_t and survived both gates, because p - base on a
 * wchar_t* divides the odd byte away.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F2)(PCWSTR, PCWSTR);

const wchar_t* wia_strstriw(const wchar_t* hay, const wchar_t* needle);
const wchar_t* ref_strstriw(const wchar_t* hay, const wchar_t* needle);
int wia_sci_init(void);
int wia_sci_match(unsigned needle, unsigned w);
extern const unsigned char wia_sci_n[];

static F2 sys;
static long cases, n_hit, n_miss;
static int failures;

#define SHY 0x00AD          /* SOFT HYPHEN -- matches a NUL (change 282's foldnul.c) */

static long off(const wchar_t* base, const wchar_t* p)
{
    return p ? (long)((const char*)p - (const char*)base) : -1;
}

static void one(const wchar_t* h, const wchar_t* need)
{
    const wchar_t* ra = wia_strstriw(h, need);
    const wchar_t* rb = sys(h, need);
    const wchar_t* rc = ref_strstriw(h, need);
    ++cases;
    if (rb) ++n_hit; else ++n_miss;
    if (off(h, ra) != off(h, rb) || off(h, ra) != off(h, rc)) {
        if (failures < 12)
            printf("  FAIL nlen=%d: ours %ld  live %ld  model %ld  (BYTE offsets)\n",
                   (int)wcslen(need), off(h, ra), off(h, rb), off(h, rc));
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
    sys = (F2)GetProcAddress(hs, "StrStrIW");
    if (!sys) { printf("no StrStrIW\n"); return 2; }
    printf("== CORRECTNESS: shlwapi!StrStrIW ==\n");
    if (wia_sci_init()) { printf("  change 281's tables disagree with the live export\n"); return 1; }
    printf("  0. change 281's match relation rebuilt and re-checked against the live export\n");

    /* 1. a 4-periodic haystack, needles of length 1..6, the match position swept */
    {
        long before = cases;
        static wchar_t h[81];
        static wchar_t n[8];
        for (k = 0; k < 80; ++k) h[k] = (wchar_t)(L'a' + (k % 4));
        h[80] = 0;
        for (j = 1; j <= 6; ++j) {
            for (m = 0; m < j; ++m) n[m] = (wchar_t)(L'A' + (m % 4));
            n[j] = 0;
            one(h, n);
            for (m = 0; m < 40; ++m) {           /* shift the needle around the period */
                int q;
                for (q = 0; q < j; ++q) n[q] = (wchar_t)(L'A' + ((m + q) % 4));
                n[j] = 0;
                one(h, n);
            }
        }
        printf("  1. a 4-periodic haystack, needles of length 1..6 at every phase: %ld\n",
               cases - before);
    }

    /* 2. every alignment x needle length x match position, with TWO matches planted so the FIRST
          is required */
    {
        long before = cases;
        static wchar_t pad[192];
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
                    one(h, n);                            /* the only match, at m */
                    /* plant a SECOND match above it: the answer must stay at m */
                    if (m + j + 4 + j <= 40) {
                        for (q = 0; q < j; ++q) h[m + j + 4 + q] = (wchar_t)(L'x' + q);
                        one(h, n);
                    }
                }
            }
        }
        printf("  2. every alignment x needle length 2..4 x match position, with a second match\n"
               "     planted above the first: %ld\n", cases - before);
    }

    /* 3. an embedded NUL ends the search */
    {
        long before = cases;
        static wchar_t h[24];
        for (k = 1; k < 20; ++k) {
            wchar_t save;
            for (m = 0; m < 20; ++m) h[m] = (wchar_t)(L'a' + m);
            h[20] = 0;
            save = h[k];
            h[k] = 0;
            one(h, L"ST");                       /* 's','t' are at 18,19 -- behind the NUL */
            one(h, L"AB");
            one(h, L"CD");
            h[k] = save;
        }
        printf("  3. an embedded NUL at every position ends the search: %ld\n", cases - before);
    }

    /* 4. ignorables matched not skipped, and the intransitive triple */
    {
        long before = cases;
        static wchar_t h1[] = { L'a', L'b', SHY, L'c', L'd', 0 };
        static wchar_t h2[] = { L'x', 0xD7A2, L'y', 0 };
        static wchar_t h3[] = { L'x', 0xD7B0, L'y', 0 };
        static wchar_t n1[] = { L'x', 0xD7B0, L'y', 0 };
        static wchar_t n2[] = { L'x', 0xD7B1, L'y', 0 };
        static wchar_t n3[] = { L'a', L'b', 0x200B, L'c', 0 };
        static wchar_t n4[] = { L'a', L'b', 0x034F, L'c', 0 };
        one(h1, L"abc");                         /* must NOT be found: no span collation */
        one(h1, L"ab");
        one(h1, L"cd");
        one(h1, n3);                             /* 0x200B matches only ITSELF */
        one(h1, n4);                             /* 0x034F IS a partner of 0x00AD */
        one(h2, n1);
        one(h2, n2);
        one(h3, n2);                             /* the third leg: must NOT match */
        printf("  4. ignorables matched not skipped, and the intransitive triple: %ld\n",
               cases - before);
    }

    /* 5. degenerate arguments */
    {
        long before = cases;
        static wchar_t t[] = L"abcXYZabc";
        static wchar_t e[] = L"";
        one(t, L"");
        one(e, L"abc");
        one(e, L"");
        one(t, L"abcXYZabcQ");                    /* longer than the haystack */
        one(t, L"Q");
        {
            const wchar_t* r1 = wia_strstriw(0, L"a");
            const wchar_t* r2 = sys(0, L"a");
            const wchar_t* r3 = ref_strstriw(0, L"a");
            ++cases;
            if (r1 || r2 || r3) { printf("  FAIL: a NULL haystack did not give NULL\n"); ++failures; }
        }
        {
            const wchar_t* r1 = wia_strstriw(t, 0);
            const wchar_t* r2 = sys(t, 0);
            const wchar_t* r3 = ref_strstriw(t, 0);
            ++cases;
            if (r1 || r2 || r3) { printf("  FAIL: a NULL needle did not give NULL\n"); ++failures; }
        }
        printf("  5. empty needle, empty string, over-long needle, NULL arguments: %ld\n",
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
                one(h, L"AB");
                one(h, L"E");
                one(h, L"#");
                one(h, L"EA");
            }
            printf("  6. every length 1..60 with the terminator last before a guard page: %ld\n",
                   cases - before);
        }
    }

    /* 7. every needle length x every interior index, as a one-character near-miss with the filter
          neutralised, plus the repaired control, on both dispatch paths */
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
                    for (k = 0; k < 48; ++k) h[k] = pad ? n[j - 1] : n[0];
                    h[48] = 0;
                    for (k = 0; k < j; ++k) h[20 + k] = n[k];
                    h[20 + p] = (wchar_t)(L'a' + j + 1);
                    one(h, n);                             /* the near-miss: NOT a match */
                    h[20 + p] = (wchar_t)(n[p] - 32);      /* the control, in the other case */
                    one(h, n);
                }
            }
        }
        for (j = 3; j <= 6; ++j) {                         /* the same on the WIDE path */
            int p;
            n[0] = SHY;
            for (m = 1; m < j; ++m) n[m] = (wchar_t)(L'a' + m);
            n[j] = 0;
            for (p = 1; p <= j - 2; ++p) {
                for (k = 0; k < 48; ++k) h[k] = 0x034F;
                h[48] = 0;
                for (k = 0; k < j; ++k) h[20 + k] = n[k];
                h[20 + p] = (wchar_t)(L'a' + j + 1);
                one(h, n);
                h[20 + p] = n[p];
                one(h, n);
            }
        }
        printf("  7. every needle length x EVERY interior index as a one-character near-miss with\n"
               "     the filter neutralised, plus its control, on both dispatches: %ld\n",
               cases - before);
    }

    /* 8. region B: a needle whose TAIL matches a NUL, so the match runs past the terminator */
    {
        long before = cases;
        static wchar_t h[64];
        static wchar_t n[8];
        int hlen = 40;
        for (j = 2; j <= 5; ++j) {
            for (k = 0; k < 64; ++k) h[k] = 0;
            for (k = 0; k < hlen; ++k) h[k] = L'z';
            h[hlen - 1] = L'q';
            h[hlen] = 0;
            n[0] = L'Q';
            for (m = 1; m < j; ++m) n[m] = SHY;
            n[j] = 0;
            one(h, n);                                     /* found at hlen-1, across the end */
            h[5] = L'q';                                   /* an EARLIER 'q' whose tail is 'z' */
            one(h, n);                                     /* still hlen-1: the first one fails */
            h[5] = L'z';
        }
        /* a needle longer than the whole string */
        for (k = 0; k < 64; ++k) h[k] = 0;
        h[0] = L'q'; h[1] = 0;
        n[0] = L'Q'; n[1] = SHY; n[2] = 0;
        one(h, n);
        n[2] = SHY; n[3] = 0;
        one(h, n);
        one(h, L"QQ");
        printf("  8. a needle whose TAIL matches a NUL, matching across the terminator, with an\n"
               "     earlier failing candidate and needles longer than the string: %ld\n",
               cases - before);
    }

    /* 9. a match planted below the haystack pointer, at every alignment.
     *
     * The forward block scan reads aligned 32-byte blocks, so the block containing the haystack
     * pointer almost always extends BELOW it, and the bottom edge mask is the only thing stopping a
     * hit there from being accepted. Change 283's equivalent corpus caught exactly that mutant, and
     * it returned a NEGATIVE byte offset, a pointer below the string the caller gave.
     */
    {
        long before = cases;
        static wchar_t buf[128];
        int a;
        for (a = 0; a < 32; ++a) {
            wchar_t* h = buf + 32 + a;
            for (k = 0; k < 128; ++k) buf[k] = L'z';
            buf[10] = L'x'; buf[11] = L'y';
            buf[20] = L'x'; buf[21] = L'y';
            buf[30] = L'x'; buf[31] = L'y';
            h[30] = 0;
            one(h, L"XY");                       /* not found: every match is below `h` */
            h[7] = L'x'; h[8] = L'y';            /* the control, above `h` */
            one(h, L"XY");
        }
        printf("  9. a match planted BELOW the haystack pointer at all 32 alignments, with its\n"
               "     found-control above it: %ld\n", cases - before);
    }

    /* 10. one needle per filter dispatch class, planted with every member of its set.
     *
     * probes/partners.c (change 283) measured that only nine partner counts occur: 0, 2, 3, 4, 5, 6,
     * 7, 8 and the 255 bitmap sentinel. A four-register filter asked to hold a five-member set must
     * drop a member, and only the dropped one exposes it, which is how a mutant that moved the
     * threshold from four to two hundred survived change 283's first sweep.
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
                if (r == 0x00AD && (seen % 101) != 1) continue;   /* 3320 members: sample */
                for (k = 0; k < 64; ++k) h[k] = 0;
                for (k = 0; k < 40; ++k) h[k] = 0xFFFD;
                h[25] = (wchar_t)mem; h[26] = L'm'; h[27] = L'n';
                h[40] = 0;
                n[0] = (wchar_t)r; n[1] = L'M'; n[2] = L'N'; n[3] = 0;
                one(h, n);                       /* found at 25 whichever member it is */
                h[26] = 0xFFFD;                  /* an interior near-miss on this path */
                one(h, n);
            }
        }
        printf("  10. one needle per filter dispatch class (partner counts 0,2,3,4,5,6,7,8 and the\n"
               "      255 sentinel), planted with every member of the set in turn: %ld\n",
               cases - before);
    }

    /* 11. a guard page where candidates FAIL and the scan must continue */
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
                h[k - 1] = L'q';                 /* the only 'q' is the LAST character */
                one(h, L"AQ");                   /* every candidate before it is rejected */
                one(h, L"AZ");                   /* rejected everywhere, up to the page */
                one(h, L"AAZ");
                one(h, L"QA");
            }
            printf("  11. a guard page with the needle's first character at nearly every position\n"
                   "      and the match at the very end or absent: %ld\n", cases - before);
        }
    }

    /* 12. NON-ZERO memory after the terminator: the virtual NUL, proved rather than assumed */
    {
        long before = cases;
        static wchar_t h[64];
        static wchar_t n[40];
        int p2, len;
        for (len = 1; len <= 12; ++len) {
            for (k = 0; k < 64; ++k) h[k] = L'W';
            for (k = 0; k < len; ++k) h[k] = (wchar_t)(L'a' + (k % 5));
            h[len] = 0;
            for (p2 = 1; p2 <= 4; ++p2) {
                n[0] = (wchar_t)((L'a' + ((len - 1) % 5)) - 32);
                for (k = 1; k <= p2; ++k) n[k] = SHY;
                n[p2 + 1] = 0;
                one(h, n);                       /* found under the virtual NUL rule */
            }
            for (p2 = 1; p2 <= 3; ++p2) {
                n[0] = (wchar_t)((L'a' + ((len - 1) % 5)) - 32);
                for (k = 1; k <= p2; ++k) n[k] = L'W';   /* the filler really IS there */
                n[p2 + 1] = 0;
                one(h, n);                       /* must NOT be found */
            }
            n[0] = (wchar_t)((L'a' + ((len - 1) % 5)) - 32);
            n[1] = SHY; n[2] = L'W'; n[3] = 0;
            one(h, n);
        }
        {
            static wchar_t m1[] = { L'B', SHY, SHY, 0 };
            static wchar_t m2[] = { L'B', SHY, L'C', 0 };
            static wchar_t m3[] = { L'B', SHY, L'W', 0 };
            for (k = 0; k < 64; ++k) h[k] = L'W';
            h[0] = L'a'; h[1] = L'b'; h[2] = 0; h[3] = L'c'; h[4] = L'd'; h[5] = 0;
            one(h, m1);                          /* the run past an EMBEDDED NUL is virtual too */
            one(h, m2);                          /* never reads the real 'c' */
            one(h, m3);
        }
        printf("  12. a haystack in a NON-ZERO buffer, with needle tails that match a NUL (found)\n"
               "      and tails that match the real filler (not found), at the terminator and at an\n"
               "      embedded NUL: %ld\n", cases - before);
    }

    /* 13. a needle whose FIRST character matches a NUL: the terminator is not a candidate */
    {
        long before = cases;
        static wchar_t h[64];
        static wchar_t n[8];
        int len;
        for (len = 1; len <= 10; ++len) {
            for (k = 0; k < 64; ++k) h[k] = L'W';
            for (k = 0; k < len; ++k) h[k] = (wchar_t)(L'a' + (k % 5));
            h[len] = 0;
            for (j = 1; j <= 3; ++j) {
                for (k = 0; k < j; ++k) n[k] = SHY;
                n[j] = 0;
                one(h, n);                       /* never a match: the terminator is not a start */
            }
            if (len >= 3) {
                h[1] = SHY;
                h[len - 1] = SHY;
                n[0] = SHY; n[1] = 0;
                one(h, n);                       /* the FIRST real one, at 1 */
                n[0] = SHY; n[1] = SHY; n[2] = 0;
                one(h, n);
            }
        }
        printf("  13. a needle whose FIRST character matches a NUL, plus planted real ones as the\n"
               "      control: %ld\n", cases - before);
    }

    /* 14. The empty needle over a haystack that contains a nul-matching code unit.
     *
     * Corpus 5 asks for an empty needle over "abcXYZabc" and gets NULL, and the first draft of this
     * change concluded that an empty needle is refused, the way change 283 correctly measured for
     * StrRStrIW. It is not. "abcXYZabc" holds no code unit that matches a NUL, and the empty needle's
     * first code unit IS the terminator, so the search finds nothing for a reason that has nothing to
     * do with the needle being empty. The live-substitution gate differed on 96 of 30000 cases, all
     * of them empty needles over haystacks that happened to contain a soft hyphen.
     *
     *     StrStrIW  with an empty needle -> the FIRST code unit matching a NUL, or NULL
     *     StrRStrIW with an empty needle -> always NULL
     *
     * So the two exports of this family genuinely differ here, and inheriting either answer is wrong.
     * A zero width space is ignorable but does NOT match a NUL, and still gives NULL, which pins the
     * behaviour on the NUL relation rather than on ignorability.
     */
    {
        long before = cases;
        static wchar_t h[32];
        int pos, pos2;
        for (pos = 0; pos < 12; ++pos) {
            for (k = 0; k < 32; ++k) h[k] = L'W';
            for (k = 0; k < 12; ++k) h[k] = (wchar_t)(L'a' + k);
            h[12] = 0;
            h[pos] = SHY;                        /* one NUL-matching unit, at a known index */
            one(h, L"");                         /* must be found AT pos */
            one(h, L"A");
            for (pos2 = pos + 1; pos2 < 12; pos2 += 5) {
                h[pos2] = SHY;                   /* a second one: the FIRST must win */
                one(h, L"");
                h[pos2] = (wchar_t)(L'a' + pos2);
            }
        }
        {
            static wchar_t z[8];
            for (k = 0; k < 32; ++k) h[k] = L'W';
            for (k = 0; k < 6; ++k) h[k] = (wchar_t)(L'a' + k);
            h[3] = 0x200B;                       /* ignorable, but NOT NUL-matching */
            h[6] = 0;
            one(h, L"");                         /* still NULL: it is the NUL relation */
            z[0] = 0; z[1] = SHY; z[2] = 0;
            one(z, L"");                         /* an empty string: no candidate position */
        }
        printf("  14. an empty needle over a haystack holding a NUL-matching code unit, at every\n"
               "      position, with a second one to force FIRST-match, and an ignorable that does\n"
               "      not match a NUL as the control: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d   (live found %ld, missed %ld)\n",
           cases, failures, n_hit, n_miss);
    if (n_hit < 1000 || n_miss < 1000) {
        printf("  the corpus did not reach both outcomes in volume\n");
        ++failures;
    }
    if (!failures)
        printf("CORRECTNESS: PASS (the returned BYTE offset exact vs live shlwapi and vs the\n"
               "scalar model, over first-match discipline, embedded NULs, the virtual NUL past the\n"
               "terminator, every filter dispatch class, ignorables, the intransitive triple and a\n"
               "guard page)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
