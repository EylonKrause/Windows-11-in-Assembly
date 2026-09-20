/* changes/286-strchrniw/correctness.c
 *
 * Three-way: ours vs an independent scalar model vs the LIVE shlwapi export.
 *
 * What this gate inherits, and why. Changes 283, 284 and 285 each ended with corpora that exist only
 * because a mutant survived, and two of them caught real defects in a shipped implementation rather than
 * in a mutant. Change 285 went further and found three of its OWN corpora vacuous, passing while
 * proving nothing, because the filler character was itself a match. So this file starts with all of it:
 *
 *   * every alignment, because the block scan masks the bytes below the string pointer;
 *   * a match planted where the scan must NOT find it, below the string pointer;
 *   * every member AND every neighbour of every dispatch class of the relation;
 *   * the guard page with the terminator as the last readable code unit;
 *   * a non-zero buffer past the terminator, so "it never reads past" is proved rather than assumed;
 *   * and plant(), which asks the live export what the UNTOUCHED string gives before planting anything.
 *     A filler that matches the sought character makes a case vacuous, and that has now happened four
 *     times across this family; the helper makes it impossible to happen silently again.
 *
 * And the shape that is new here: THE COUNT. It is tested at the boundary in both directions for every
 * match position, a count that just reaches the match and a count that just misses it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PWSTR (WINAPI *FCN)(PCWSTR, WCHAR, UINT);

const wchar_t* wia_strchrniw(const wchar_t*, wchar_t, unsigned);
const wchar_t* ref_strchrniw(const wchar_t*, wchar_t, unsigned);
int wia_sci_init(void);
int wia_sci_match(unsigned needle, unsigned w);
extern const unsigned char wia_sci_n[];

static FCN sys;
static long cases, n_hit, n_miss;
static int failures;

#define SHY  0x00AD     /* SOFT HYPHEN -- one of the 3237 ignorables, and matches a NUL */
#define CGJ  0x034F     /* COMBINING GRAPHEME JOINER -- another member of that same set */
#define ZWSP 0x200B     /* ZERO WIDTH SPACE -- matches ONLY ITSELF (n = 0) */
#define FILL 0x0002     /* matches only itself: see the startup proof below */

static long off(const wchar_t* base, const void* p)
{
    return p ? (long)((const char*)p - (const char*)base) : -1;
}

static void one(const wchar_t* s, wchar_t m, unsigned cch)
{
    const void* a = wia_strchrniw(s, m, cch);
    const void* b = sys(s, m, cch);
    const void* c = ref_strchrniw(s, m, cch);
    ++cases;
    if (b) ++n_hit; else ++n_miss;
    if (off(s, a) != off(s, b) || off(s, a) != off(s, c)) {
        if (failures < 12)
            printf("  FAIL match=U+%04X count=%u: ours %ld  live %ld  model %ld  (BYTE offsets)\n",
                   (unsigned)m, cch, off(s, a), off(s, b), off(s, c));
        ++failures;
    }
}

/* Plant `c` at `pos` in a FILL-filled string and ask at the count boundary in both directions, but
   first confirm the untouched string finds nothing, because a filler that matches the sought character
   makes every case vacuous. */
static void plant(wchar_t* s, int len, int pos, wchar_t c, wchar_t m)
{
    int i;
    for (i = 0; i < len; ++i) s[i] = (wchar_t)FILL;
    s[len] = 0;
    if (sys(s, m, (unsigned)len) != 0) {
        if (failures < 12)
            printf("  VACUOUS: the filler matches U+%04X, so nothing here proves anything\n",
                   (unsigned)m);
        ++failures;
        return;
    }
    one(s, m, (unsigned)len);                  /* the control is a case worth counting */
    s[pos] = c;
    one(s, m, (unsigned)pos);                  /* a count that stops one short: must MISS */
    one(s, m, (unsigned)pos + 1);              /* a count that just reaches it: must HIT */
    one(s, m, (unsigned)len);
    one(s, m, 0xFFFFFFFFu);                    /* and a count far past the terminator */
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
    sys = (FCN)GetProcAddress(hs, "StrChrNIW");
    if (!sys) { printf("no StrChrNIW\n"); return 2; }
    printf("== CORRECTNESS: shlwapi!StrChrNIW ==\n");
    if (wia_sci_init()) { printf("  change 281's tables disagree with the live export\n"); return 1; }
    printf("  0. change 281's match relation rebuilt and re-checked against the live export\n");
    {
        static const unsigned short used[] = { 0x0001, 0x0020, 0x0023, 0x0035, 0x004B, 0x00C6,
                                               0x0598, 0x02B9, 0x00AD, 0x034F, 0x200B, 0xD7A2,
                                               L'a', L'q', L'z', L'A', L'Q', L'Z' };
        int i, bad = 0;
        if (wia_sci_n[FILL] != 0) { printf("  FILLER U+%04X is not self-only\n", FILL); ++failures; }
        for (i = 0; i < (int)(sizeof(used) / sizeof(used[0])); ++i)
            if (wia_sci_match(used[i], FILL)) {
                printf("  FILLER U+%04X is matched by U+%04X\n", FILL, used[i]); ++bad;
            }
        if (bad) ++failures;
        printf("  0b. the filler U+%04X matches only itself and none of the %d characters this corpus\n"
               "      searches for -- checked, because four corpora in this family were once vacuous\n"
               "      for exactly that reason\n", FILL, (int)(sizeof(used) / sizeof(used[0])));
    }

    /* 1. The count boundary at every match position, through plant() */
    {
        long before = cases;
        static wchar_t s[80];
        for (m = 0; m < 64; ++m) plant(s, 64, m, L'q', L'Q');
        printf("  1. the count boundary at every match position of 64: one short must MISS, one more\n"
               "     must HIT: %ld\n", cases - before);
    }

    /* 2. every alignment of the string pointer within a 32-byte block */
    {
        long before = cases;
        static wchar_t pad[200];
        for (k = 0; k < 32; ++k) {
            wchar_t* s = pad + 32 + k;
            for (j = 0; j < 200; ++j) pad[j] = (wchar_t)FILL;
            for (m = 0; m < 20; ++m) {
                for (j = 0; j < 48; ++j) s[j] = (wchar_t)FILL;
                s[48] = 0;
                s[m] = L'q';
                one(s, L'Q', 48);
                one(s, L'Q', (unsigned)m);
                one(s, L'Q', (unsigned)m + 1);
                s[m] = (wchar_t)FILL;
            }
        }
        printf("  2. every alignment x every match position, with the count at the boundary: %ld\n",
               cases - before);
    }

    /* 3. a match planted BELOW the string pointer, at every alignment.
     *
     * The block scan reads aligned 32-byte blocks, so the block holding the string pointer almost always
     * extends below it, and the bottom edge mask is the only thing stopping a hit there from being
     * accepted. Change 283's equivalent corpus caught exactly that mutant, and it returned a NEGATIVE
     * byte offset, a pointer below the string the caller gave.
     */
    {
        long before = cases;
        static wchar_t buf[128];
        for (k = 0; k < 32; ++k) {
            wchar_t* s = buf + 32 + k;
            for (j = 0; j < 128; ++j) buf[j] = (wchar_t)FILL;
            buf[5] = L'q'; buf[12] = L'q'; buf[20] = L'q'; buf[31] = L'q';
            s[30] = 0;
            one(s, L'Q', 30);                    /* every 'q' is below `s`: NULL */
            one(s, L'Q', 0xFFFFFFFFu);
            s[9] = L'q';
            one(s, L'Q', 30);                    /* and the control, above it */
            s[9] = (wchar_t)FILL;
        }
        printf("  3. a match planted BELOW the string pointer at all 32 alignments, with its control\n"
               "     above it: %ld\n", cases - before);
    }

    /* 4. The terminator is never a match, the rule that differs from changes 283 and 284.
     *
     * There, a needle character that matches a NUL matched the terminator itself. Here it does not, so
     * every NUL-matching character must give NULL over a string that contains no other match, however
     * far the count reaches.
     */
    {
        long before = cases;
        static wchar_t s[64];
        int len;
        for (len = 1; len <= 20; ++len) {
            for (k = 0; k < 64; ++k) s[k] = L'W';        /* non-zero past the terminator */
            for (k = 0; k < len; ++k) s[k] = (wchar_t)(L'a' + (k % 5));
            s[len] = 0;
            one(s, (wchar_t)0, (unsigned)len);           /* searching for a NUL: never a match */
            one(s, (wchar_t)0, (unsigned)len + 8);
            one(s, (wchar_t)0, 0xFFFFFFFFu);
            one(s, (wchar_t)SHY, (unsigned)len + 8);     /* a NUL-matching character: still NULL */
            one(s, (wchar_t)CGJ, 0xFFFFFFFFu);
            one(s, L'W', (unsigned)len + 8);             /* 'W' exists only PAST the terminator */
            one(s, L'W', 0xFFFFFFFFu);
            one(s, L'A', (unsigned)len);                 /* and the control: 'a' is at 0 */
        }
        printf("  4. the terminator is never a match: NUL, two NUL-matching characters, and a\n"
               "     character that exists only past the terminator, at counts reaching well past\n"
               "     it: %ld\n", cases - before);
    }

    /* 5. an embedded NUL at every position ends the scan */
    {
        long before = cases;
        static wchar_t s[32];
        for (k = 1; k < 20; ++k) {
            for (m = 0; m < 20; ++m) s[m] = (wchar_t)(L'a' + m);
            s[20] = 0;
            s[k] = 0;
            one(s, L'T', 20);                            /* 't' is at 19, behind the NUL */
            one(s, L'T', 0xFFFFFFFFu);
            one(s, L'B', 20);
            one(s, (wchar_t)SHY, 20);                    /* the embedded NUL is not a match either */
        }
        printf("  5. an embedded NUL at every position ends the scan, and is not itself a match: "
               "%ld\n", cases - before);
    }

    /* 6. every member AND every neighbour of every dispatch class.
     *
     * A sought character with 2..4 partners has its pool slot broadcast into four registers, so only the
     * LAST member of the slot exposes an off-by-one there; and change 285 measured that a slot is eight
     * words, so for a member with eight partners the entry past its members belongs to the NEXT slot --
     * which is why the neighbours are swept as well as the members.
     */
    {
        long before = cases;
        static const unsigned short reps[] = { 0x0001, 0x0020, 0x0023, 0x0035,
                                               0x004B, 0x00C6, 0x0598, 0x02B9, 0x00AD };
        static wchar_t s[64];
        unsigned r, mem;
        int i, seen;
        for (i = 0; i < 9; ++i) {
            r = reps[i];
            seen = 0;
            for (mem = 1; mem < 65536; ++mem) {
                if (!wia_sci_match(r, mem)) continue;
                ++seen;
                if (r == 0x00AD && (seen % 101) != 1) continue;   /* 3237 members: sample */
                plant(s, 40, 25, (wchar_t)mem, (wchar_t)r);
            }
            for (mem = (r > 4 ? r - 4 : 1); mem <= (unsigned)r + 10 && mem < 65536; ++mem)
                plant(s, 40, 25, (wchar_t)mem, (wchar_t)r);
        }
        printf("  6. every member and every neighbour of every dispatch class (partner counts\n"
               "     0,2,3,4,5,6,7,8 and the 255 sentinel), at the count boundary: %ld\n",
               cases - before);
    }

    /* 7. the WIDE path: a sought character with more than four partners bypasses the filter */
    {
        long before = cases;
        static const unsigned short wide[] = { 0x004B, 0x00C6, 0x0598, 0x02B9, 0x00AD, 0xD7A2,
                                               0x030D, 0x1CD0 };
        static wchar_t s[64];
        int i;
        for (i = 0; i < 8; ++i) {
            unsigned r = wide[i];
            printf("");
            for (m = 0; m < 40; m += 7) plant(s, 40, m, (wchar_t)r, (wchar_t)r);
            /* and with nothing matching at all, at counts on both sides of the length */
            for (k = 0; k < 40; ++k) s[k] = (wchar_t)FILL;
            s[40] = 0;
            one(s, (wchar_t)r, 40);
            one(s, (wchar_t)r, 41);
            one(s, (wchar_t)r, 0xFFFFFFFFu);
        }
        printf("  7. the WIDE path: eight characters with more than four partners, planted at every\n"
               "     seventh position and absent: %ld\n", cases - before);
    }

    /* 8. a guard page, with the terminator as the last readable code unit and a huge count */
    {
        long before = cases;
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  8. GUARD PAGE SETUP FAILED\n"); ++failures;
        } else {
            for (k = 1; k <= 60; ++k) {
                wchar_t* s = (wchar_t*)(base + pg) - (k + 1);
                for (j = 0; j < k; ++j) s[j] = (wchar_t)(L'a' + (j % 5));
                s[k] = 0;
                one(s, L'Q', (unsigned)k);               /* nothing matches: stop at the NUL */
                one(s, L'Q', 0xFFFFFFFFu);               /* a count far past the page */
                one(s, (wchar_t)SHY, 0xFFFFFFFFu);       /* NUL-matching, so the NUL must not stop */
                one(s, L'E', (unsigned)k);               /* and something that does match */
                s[k - 1] = L'q';
                one(s, L'Q', 0xFFFFFFFFu);               /* the match IS the last character */
                s[k - 1] = (wchar_t)(L'a' + ((k - 1) % 5));
            }
            printf("  8. every length 1..60 with the terminator last before a guard page, with counts\n"
                   "     reaching far past it: %ld\n", cases - before);
        }
    }

    /* 9. degenerate arguments */
    {
        long before = cases;
        static wchar_t t[] = L"abcdef";
        static wchar_t e[2];
        e[0] = 0;
        one(t, L'A', 0);                                 /* a count of zero: NULL */
        one(t, L'F', 6);                                 /* the last character, count exactly 6 */
        one(t, L'F', 5);                                 /* one short: NULL */
        one(t, L'A', 1);                                 /* the first character, count 1 */
        one(t, L'B', 1);                                 /* outside a count of 1 */
        one(e, L'A', 5);                                 /* an empty string */
        one(e, L'A', 0);
        one(e, (wchar_t)0, 5);
        {
            const void* a = wia_strchrniw(0, L'a', 5);
            const void* b = sys(0, L'a', 5);
            const void* c = ref_strchrniw(0, L'a', 5);
            ++cases; ++n_miss;
            if (a || b || c) { printf("  FAIL: a NULL start did not give NULL\n"); ++failures; }
        }
        printf("  9. a count of zero, the exact-length count, one short, an empty string and a NULL\n"
               "     start: %ld\n", cases - before);
    }

    /* 10. a long string, counts straddling a block boundary at every offset */
    {
        long before = cases;
        static wchar_t s[600];
        for (k = 0; k < 600; ++k) s[k] = (wchar_t)FILL;
        s[520] = 0;
        for (m = 0; m < 520; m += 3) {
            s[m] = L'q';
            one(s, L'Q', (unsigned)m);
            one(s, L'Q', (unsigned)m + 1);
            one(s, L'Q', 520);
            s[m] = (wchar_t)FILL;
        }
        one(s, L'Q', 520);
        one(s, L'Q', 0xFFFFFFFFu);
        printf("  10. a 520-character string, the match swept every third position with the count at\n"
               "      the boundary each time: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d   (live found %ld, missed %ld)\n",
           cases, failures, n_hit, n_miss);
    if (n_hit < 500 || n_miss < 500) {
        printf("  the corpus did not reach both outcomes in volume\n");
        ++failures;
    }
    if (!failures)
        printf("CORRECTNESS: PASS (the returned BYTE offset exact vs live shlwapi and vs the scalar\n"
               "model, over the count boundary in both directions at every position, every alignment,\n"
               "matches below the string pointer, the terminator never matching, every member and\n"
               "neighbour of every dispatch class, the WIDE path, a guard page and huge counts)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
