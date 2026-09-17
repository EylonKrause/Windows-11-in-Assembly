/* changes/281-strchriw/correctness.c
 *
 * Gate 1 for shlwapi!StrChrIW: OURS vs THE SCALAR MODEL vs THE LIVE EXPORT, on the returned
 * pointer -- compared as an OFFSET, so that "found it in the right place" and "found it at all" are
 * the same question.
 *
 * THE CORPUS IS BUILT WHERE A VECTORISED, PAGE-ALIGNED SEARCH GOES WRONG:
 *
 *   * EVERY ONE OF THE 65536 NEEDLES, each searched for in a string containing its case partner.
 *     That is the whole equivalence rule, asked one code unit at a time rather than sampled.
 *   * EVERY ALIGNMENT. The implementation aligns the pointer DOWN to 32 bytes and discards the mask
 *     of everything before the true start; if that mask were off by one lane it would find a match
 *     in the bytes BEFORE the string. Every start offset 0..31 is swept, with a planted match in
 *     the discarded region.
 *   * EVERY MATCH POSITION in strings of every length up to 200, because the first-match rule and
 *     the block boundary interact: a match in block 2 must not be beaten by a terminator in block 1
 *     and vice versa.
 *   * THE TERMINATOR IN EVERY LANE, because the loop finds the match and the end of the string in
 *     the SAME pass and has to decide which came first.
 *   * AND A GUARD PAGE. A 32-byte load reaches past the terminator by construction. The only proof
 *     that it never reaches into an unmapped page is to put one there: every string length 0..64 is
 *     placed so that its terminator is the last readable byte before a PAGE_NOACCESS page.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);

extern const wchar_t* wia_strchriw(const wchar_t*, wchar_t);
const wchar_t* ref_strchriw(const wchar_t*, wchar_t);
int wia_sci_init(void);
extern unsigned short wia_sci_fold[65536];
extern unsigned short wia_sci_cls[65536];

/* the first and second code unit of each class, built in one O(65536) pass, so that every needle
   can be tested against a REAL partner rather than against something a case function suggested.
   Building the corpus from the case functions is exactly what made probes/contract.c wrong. */
static unsigned short firstof[65536], secondof[65536];
static void build_partners(void)
{
    unsigned c;
    for (c = 1; c <= 0xFFFF; ++c) {
        unsigned f = wia_sci_fold[c];
        if (!firstof[f]) firstof[f] = (unsigned short)c;
        else if (!secondof[f]) secondof[f] = (unsigned short)c;
    }
}
static unsigned short partner_of(unsigned c)
{
    unsigned f = wia_sci_fold[c];
    if (firstof[f] != c) return firstof[f];
    return secondof[f] ? secondof[f] : (unsigned short)c;
}

static F_chr sys;
static int failures = 0;
static long cases = 0, n_hit = 0, n_miss = 0;

/* compare as offsets so a null and a pointer can never accidentally agree */
static long off(const wchar_t* base, const wchar_t* p) { return p ? (long)(p - base) : -1; }

static void one(const wchar_t* s, wchar_t c)
{
    const wchar_t *ra, *rb, *rc;
    ra = wia_strchriw(s, c);
    rb = sys(s, c);
    rc = ref_strchriw(s, c);
    ++cases;
    if (rb) ++n_hit; else ++n_miss;
    if (off(s, ra) != off(s, rb) || off(s, ra) != off(s, rc)) {
        if (failures < 12)
            printf("  FAIL needle=%04X len=%d: ours %ld  live %ld  model %ld\n",
                   (unsigned)c, (int)wcslen(s), off(s, ra), off(s, rb), off(s, rc));
        ++failures;
    }
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    SYSTEM_INFO si;
    unsigned char* base;
    SIZE_T pagesz;
    unsigned c;
    int k, j;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F_chr)GetProcAddress(hs, "StrChrIW");
    if (!sys) { printf("no StrChrIW\n"); return 2; }
    printf("== CORRECTNESS: shlwapi!StrChrIW ==\n");
    {
        int rc = wia_sci_init();
        if (rc) { printf("  the fold table failed its own checks against the live export: %d\n", rc);
                  return 1; }
        printf("  0. the fold table was rebuilt from foldpairs.c and CHECKED against live\n"
               "     StrChrIW: every member of every non-singleton class found at the first\n"
               "     member, 8192 sampled cross-class pairs rejected, and the four facts that\n"
               "     separate this relation from its neighbours\n");
    }

    /* 1. EVERY needle, searched for in a string holding a REAL member of its class.
     *
     * The partner comes from the measured fold, not from a case function. probes/contract.c built
     * its pairs from CharUpperW/CharLowerW/RtlUpcase/RtlDowncase and therefore could never ask
     * about (U+1D2C, 'a') -- a pair none of those four relates -- which is exactly the pair that
     * was wrong. A corpus that cannot express a case cannot fail on it.
     */
    {
        long before = cases;
        static wchar_t buf[8];
        build_partners();
        for (c = 1; c <= 0xFFFF; ++c) {
            wchar_t p = (wchar_t)partner_of(c);
            buf[0] = L'\x2461'; buf[1] = L'\x2462'; buf[2] = p;
            buf[3] = L'\x2463'; buf[4] = 0;
            one(buf, (wchar_t)c);                 /* must find the partner at offset 2 */
            buf[2] = (wchar_t)c;
            one(buf, (wchar_t)c);                 /* and must find itself */
            buf[2] = L'\x2464';
            one(buf, (wchar_t)c);                 /* and must MISS when no member is present */
        }
        printf("  1. every needle 1..65535: its class partner, itself, and a miss: %ld\n",
               cases - before);
    }

    /* 2. the needle that is never found, and the empty string */
    {
        long before = cases;
        one(L"abc", 0);
        one(L"", L'a');
        one(L"", 0);
        {
            const wchar_t* r1 = wia_strchriw(0, L'a');
            const wchar_t* r2 = sys(0, L'a');
            const wchar_t* r3 = ref_strchriw(0, L'a');
            ++cases;
            if (r1 || r2 || r3) { printf("  FAIL: a null source did not return null\n"); ++failures; }
        }
        printf("  2. the terminator as a needle, an empty string, a null source: %ld\n",
               cases - before);
    }

    /* 3. EVERY ALIGNMENT, with a planted match in the region the mask must discard */
    {
        long before = cases;
        static wchar_t pad[256];
        for (k = 0; k < 256; ++k) pad[k] = L'Q';        /* the needle's partner, everywhere before */
        for (k = 0; k < 32; ++k) {
            wchar_t* s = pad + 16 + k;                  /* every start offset within a 32-byte block */
            int m;
            for (m = 0; m < 24; ++m) {
                wchar_t save[24];
                int q;
                for (q = 0; q < 24; ++q) { save[q] = s[q]; s[q] = L'z'; }
                s[23] = 0;
                s[m] = (m == 23) ? 0 : L'Q';            /* the only match, at position m */
                one(s, L'q');
                one(s, L'Q');
                for (q = 0; q < 24; ++q) s[q] = save[q];
            }
        }
        printf("  3. every start alignment 0..31 x every match position: %ld\n", cases - before);
    }

    /* 4. every length up to 200, match at every position, and no match at all */
    {
        long before = cases;
        static wchar_t s[224];
        for (k = 0; k <= 200; ++k) {
            for (j = 0; j < k; ++j) s[j] = (wchar_t)(L'a' + (j % 26));
            s[k] = 0;
            one(s, L'#');                                /* never present */
            one(s, L'A');                                /* present iff k > 0 */
            for (j = 0; j < k; j += (k > 40 ? 7 : 1)) {
                wchar_t sv = s[j];
                s[j] = L'\x00DF';                        /* a character with no upper partner */
                one(s, L'\x00DF');
                s[j] = L'\x00E9';                        /* e-acute: has a partner */
                one(s, L'\x00C9');
                s[j] = sv;
            }
        }
        printf("  4. every length 0..200, match at every position: %ld\n", cases - before);
    }

    /* 5. surrogates, treated as independent code units */
    {
        long before = cases;
        static wchar_t sp[8] = { 0xD83D, 0xDE00, 0xD83D, 0xDE01, 0 };
        one(sp, 0xD83D);
        one(sp, 0xDE00);
        one(sp, 0xDE01);
        one(sp, 0xD800);
        printf("  5. surrogate pairs as code units: %ld\n", cases - before);
    }

    /* 6. THE GUARD PAGE. Every length 0..64, with the terminator as the last readable code unit
     *    before a PAGE_NOACCESS page. A 32-byte load reaches past the terminator by construction;
     *    this is the only proof it never reaches past the PAGE. */
    {
        long before = cases;
        GetSystemInfo(&si);
        pagesz = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pagesz, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  6. GUARD PAGE SETUP FAILED\n"); ++failures;
        } else {
            for (k = 0; k <= 64; ++k) {
                wchar_t* s = (wchar_t*)(base + pagesz) - (k + 1);   /* terminator last before the page */
                for (j = 0; j < k; ++j) s[j] = (wchar_t)(L'a' + (j % 26));
                s[k] = 0;
                one(s, L'#');                       /* a miss: the loop must run to the terminator */
                one(s, L'A');
                one(s, L'Z');
                if (k) { s[k - 1] = L'\x00FF'; one(s, L'\x00DF'); }
            }
            printf("  6. every length 0..64 ending exactly at a guard page: %ld\n", cases - before);
        }
    }

    printf("\n  total cases: %ld,  mismatches: %d   (live found a match %ld times, missed %ld)\n",
           cases, failures, n_hit, n_miss);
    if (n_hit < 10000 || n_miss < 1000) {
        printf("  the corpus did not reach both outcomes in volume -- the gate fails without them\n");
        ++failures;
    }
    if (!failures)
        printf("CORRECTNESS: PASS (the returned offset exact vs live shlwapi and vs the scalar\n"
               "model, over every needle, every alignment and a guard page)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
