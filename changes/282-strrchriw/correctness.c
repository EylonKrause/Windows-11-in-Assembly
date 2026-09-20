/* changes/282-strrchriw/correctness.c
 *
 * Gate 1 for shlwapi!StrRChrIW: Ours vs the scalar model vs the live export, on the returned
 * offset, so "found it" and "found it in the right place" are one question.
 *
 * The corpus is built where a backward, range-masked, vectorised search goes wrong:
 *
 *   * Both edge masks, independently and together. The top block discards bytes at or after `end`
 *     and the bottom block discards bytes before `start`; when the range fits inside ONE 32-byte
 *     block both apply at once, which is the case a mask written for two separate blocks gets
 *     wrong. Every (start alignment, length) pair up to 80 is swept, so every combination of the
 *     two masks occurs, including every range that lives inside a single block.
 *   * The last match, not the first. a forward scan that happened to return a match would pass any
 *     test with one match in it, so matches are planted at every position of every range.
 *   * Every needle 0..65535, each searched for against a real partner drawn from the measured
 *     relation rather than from a case function, building a corpus from case functions is what
 *     made change 281's contract probe wrong.
 *   * EMBEDDED NULs, because this export has no terminator: probes/bounds.c measured that
 *     "abcd\0fghijk" with end = start+11 finds 'J' at 9. An implementation that stopped at a NUL
 *     would pass a corpus made only of ordinary strings.
 *   * And a guard page on both sides. The range is explicit, so the scan may not read before
 *     `start` or at or after `end`. Ranges are placed hard against a PAGE_NOACCESS page at each
 *     end in turn.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef PCWSTR (WINAPI *F_rchr)(PCWSTR, PCWSTR, WCHAR);

extern const wchar_t* wia_strrchriw(const wchar_t*, const wchar_t*, wchar_t);
const wchar_t* ref_strrchriw(const wchar_t*, const wchar_t*, wchar_t);
int wia_sci_init(void);
unsigned wia_sci_partner(unsigned c);
extern unsigned char wia_sci_n[65536];

static F_rchr sys;
static int failures = 0;
static long cases = 0, n_hit = 0, n_miss = 0;
static long n_self = 0, n_small = 0, n_mid = 0, n_big = 0;

/* The offset is measured in bytes, not code units, and that is not pedantry.
 *
 * A mutant that dropped `and ecx, -2`, the rounding that turns BSR's high-byte index back into
 * the start of the word, survived both gates. BSR reports the high byte of a matching word, so
 * without that rounding the returned pointer is off by ONE BYTE, into the middle of a wchar_t. And
 * `p - base` on a wchar_t* divides the difference by two, which throws the odd byte away: the two
 * pointers compare EQUAL as code-unit offsets while being different addresses.
 *
 * That is change 268's lesson in another costume, 154 mismatches in change 016 that were nothing
 * but a single 00 past the end of a string. Comparing the thing the caller actually receives, in
 * bytes, is the only way to see it. */
static long off(const wchar_t* base, const wchar_t* p)
{ return p ? (long)((const char*)p - (const char*)base) : -1; }

static void one(const wchar_t* s, const wchar_t* e, wchar_t c)
{
    const wchar_t *ra, *rb, *rc;
    unsigned n;
    ra = wia_strrchriw(s, e, c);
    rb = sys(s, e, c);
    rc = ref_strrchriw(s, e, c);
    ++cases;
    if (rb) ++n_hit; else ++n_miss;
    n = wia_sci_n[(unsigned short)c];
    if (n == 0) ++n_self; else if (n == 255) ++n_big; else if (n <= 4) ++n_small; else ++n_mid;
    if (off(s, ra) != off(s, rb) || off(s, ra) != off(s, rc)) {
        if (failures < 12)
            printf("  FAIL needle=%04X len=%d: ours %ld  live %ld  model %ld  (BYTE offsets)\n",
                   (unsigned)c, (int)(e - s), off(s, ra), off(s, rb), off(s, rc));
        ++failures;
    }
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    SYSTEM_INFO si;
    unsigned char* base;
    SIZE_T pg;
    unsigned c;
    int k, j, m;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F_rchr)GetProcAddress(hs, "StrRChrIW");
    if (!sys) { printf("no StrRChrIW\n"); return 2; }
    printf("== CORRECTNESS: shlwapi!StrRChrIW ==\n");
    if (wia_sci_init()) { printf("  change 281's tables disagree with the live export\n"); return 1; }
    printf("  0. change 281's match relation rebuilt and re-checked against the live export\n");

    /* 1. every needle, against a real partner and against itself, plus a miss */
    {
        long before = cases;
        static wchar_t buf[8];
        for (c = 0; c <= 0xFFFF; ++c) {
            wchar_t p = (wchar_t)wia_sci_partner(c);
            if (!c && !p) continue;
            buf[0] = L'\x2461'; buf[1] = L'\x2462'; buf[2] = p; buf[3] = L'\x2463';
            one(buf, buf + 4, (wchar_t)c);
            buf[2] = (wchar_t)c;
            one(buf, buf + 4, (wchar_t)c);
            buf[2] = L'\x2464';
            one(buf, buf + 4, (wchar_t)c);
        }
        printf("  1. every needle 0..65535: a real partner, itself, and a miss: %ld\n",
               cases - before);
    }

    /* 2. The two edge masks: every start alignment x every length, with a match at every position */
    {
        long before = cases;
        static wchar_t pad[256];
        for (k = 0; k < 256; ++k) pad[k] = L'Q';     /* a partner of 'q' everywhere OUTSIDE too */
        for (k = 0; k < 32; ++k) {
            wchar_t* s = pad + 32 + k;
            for (j = 1; j <= 80; ++j) {
                /* no match inside: every 'Q' outside the range must be invisible */
                for (m = 0; m < j; ++m) s[m] = L'z';
                one(s, s + j, L'q');
                /* a match at every position, one at a time: the LAST must win */
                for (m = 0; m < j; m += (j > 20 ? 5 : 1)) {
                    s[m] = L'Q';
                    one(s, s + j, L'q');
                    s[m] = L'z';
                }
                /* two matches: the later one must win */
                if (j >= 4) {
                    s[0] = L'Q'; s[j - 2] = L'Q';
                    one(s, s + j, L'q');
                    s[0] = L'z'; s[j - 2] = L'z';
                }
                for (m = 0; m < j; ++m) s[m] = L'Q';  /* restore the surround */
            }
        }
        printf("  2. every start alignment 0..31 x every length 1..80, match at every position,\n"
               "     with matches planted OUTSIDE the range that must stay invisible: %ld\n",
               cases - before);
    }

    /* 3. embedded NULs; this export has no terminator */
    {
        long before = cases;
        static wchar_t emb[24];
        for (k = 0; k < 20; ++k) emb[k] = (wchar_t)(L'a' + k);
        for (k = 0; k < 20; ++k) {
            wchar_t save = emb[k];
            emb[k] = 0;
            one(emb, emb + 20, L'T');            /* 't' is at index 19 */
            one(emb, emb + 20, 0);               /* and the NUL itself is an ordinary needle */
            emb[k] = save;
        }
        printf("  3. a NUL at every position, the range honoured through it: %ld\n", cases - before);
    }

    /* 4. empty and inverted ranges, and NULL arguments */
    {
        long before = cases;
        static wchar_t s[] = L"abc";
        one(s, s, L'a');
        one(s + 1, s + 1, L'a');
        one(s, s + 1, L'A');
        {
            const wchar_t* r1 = wia_strrchriw(0, 0, L'a');
            const wchar_t* r2 = sys(0, 0, L'a');
            const wchar_t* r3 = ref_strrchriw(0, 0, L'a');
            ++cases;
            if (r1 || r2 || r3) { printf("  FAIL: NULL did not return NULL\n"); ++failures; }
        }
        printf("  4. empty ranges and NULL arguments: %ld\n", cases - before);
    }

    /* 5. Guard pages at both ends. The scan may not read at or after `end`, nor before `start`. */
    {
        long before = cases;
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        /* (a) the range ends exactly at a guard page */
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  5. GUARD PAGE SETUP FAILED\n"); ++failures;
        } else {
            for (k = 1; k <= 80; ++k) {
                wchar_t* s = (wchar_t*)(base + pg) - k;
                for (j = 0; j < k; ++j) s[j] = (wchar_t)(L'a' + (j % 26));
                one(s, s + k, L'#');
                one(s, s + k, L'A');
                one(s, s + k, (wchar_t)(L'A' + ((k - 1) % 26)));
            }
            printf("  5a. every length 1..80 ending exactly at a guard page: %ld\n", cases - before);
        }
        /* (b) the range STARTS exactly after a guard page */
        {
            unsigned char* b2 = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
            long before2 = cases;
            if (b2 && VirtualAlloc(b2 + pg, pg, MEM_COMMIT, PAGE_READWRITE)) {
                for (k = 1; k <= 80; ++k) {
                    wchar_t* s = (wchar_t*)(b2 + pg);
                    for (j = 0; j < k; ++j) s[j] = (wchar_t)(L'a' + (j % 26));
                    one(s, s + k, L'#');
                    one(s, s + k, L'A');
                }
                printf("  5b. every length 1..80 starting exactly after a guard page: %ld\n",
                       cases - before2);
            } else printf("  5b. second guard page setup failed\n");
        }
    }

    printf("\n  total cases: %ld,  mismatches: %d   (live found %ld, missed %ld)\n",
           cases, failures, n_hit, n_miss);
    printf("  needle shapes reached: self-only %ld, 2-4 partners %ld, 5-8 %ld, bitmap %ld\n",
           n_self, n_small, n_mid, n_big);
    if (n_hit < 10000 || n_miss < 5000 || n_self < 1000 || n_small < 1000 || n_mid < 100 || n_big < 100) {
        printf("  the corpus did not reach every outcome and every dispatch path\n");
        ++failures;
    }
    if (!failures)
        printf("CORRECTNESS: PASS (the returned offset exact vs live shlwapi and vs the scalar\n"
               "model, over every needle, both edge masks, embedded NULs and guard pages at both\n"
               "ends of the range)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
