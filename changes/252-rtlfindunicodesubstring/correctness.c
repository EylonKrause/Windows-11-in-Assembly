/* changes/252-rtlfindunicodesubstring/correctness.c
 *
 * THREE-WAY: ours vs the independent oracle vs the LIVE ntdll export on this machine. All three
 * must agree on every case, and the answer compared is the OFFSET of the hit (or -1), not a raw
 * pointer, so a correct answer cannot hide behind a pointer that happens to be equal.
 *
 * The structure of this file is a response to how change 248 Failed. There, 1,085,965 enumerated
 * cases passed in silence while the vectorised path was broken, because a six-character string
 * never reaches the vector path at all, the enumeration proved the scalar tail and nothing else.
 * So the corpora here are split by which path they reach, and each section says which:
 *
 *   1. EXHAUSTIVE, short, reaches only the scalar tail (n-m < 15)
 *   2. The vector-loop boundary; every n-m from 0 to 40, which is where the loop turns on
 *   3. RANDOMISED, long, reaches the vector loop, with planted and absent needles
 *   4. The fold, at the hard pairs, non-ASCII partners no ASCII fold brings together, and
 *                                      pairs the NT ordinal table deliberately does NOT merge
 *                                      (U+017F, U+0130/U+0131, U+00DF all upcase to themselves --
 *                                      the ordinal table is much narrower than Unicode case
 *                                      folding, and a fold that merged them would be wrong)
 *   5. A GUARD PAGE, proves the "no clamp is needed" claim by making an
 *                                      over-read FAULT rather than merely disagree
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef PWSTR (NTAPI *FFIND)(USTR*, USTR*, BOOLEAN);

PWSTR wia_findunicodesubstring(void* Full, void* Search, BOOLEAN CaseInSensitive);
void  ref_init(void);
PWSTR ref_findunicodesubstring(void* Full, void* Search, BOOLEAN CaseInSensitive);
int   wia_casemate_init(void);      /* returns the largest case class it saw -- must be 2 */

static FFIND live;
static long  fails = 0, cases = 0;

static int off(PWSTR r, PWSTR base) { return r ? (int)(r - base) : -1; }

/* One case, all three ways. */
static void one(wchar_t* h, int hn, wchar_t* n, int nn, int ci, const char* where)
{
    USTR H, N;
    int a, b, c;
    H.Buffer = h; H.Length = (USHORT)(hn * 2); H.MaximumLength = H.Length;
    N.Buffer = n; N.Length = (USHORT)(nn * 2); N.MaximumLength = N.Length;
    ++cases;
    a = off(wia_findunicodesubstring(&H, &N, (BOOLEAN)ci), h);
    b = off(ref_findunicodesubstring(&H, &N, (BOOLEAN)ci), h);
    c = off(live(&H, &N, (BOOLEAN)ci), h);
    if (a != b || a != c) {
        if (++fails <= 25) {
            int i;
            printf("  MISMATCH [%s] ci=%d  ours=%d oracle=%d live=%d\n", where, ci, a, b, c);
            printf("           haystack(%d) =", hn);
            for (i = 0; i < hn && i < 48; ++i) printf(" %04X", h[i]);
            printf("\n           needle  (%d) =", nn);
            for (i = 0; i < nn && i < 48; ++i) printf(" %04X", n[i]);
            printf("\n");
        }
    }
}

static unsigned long long rs = 0x243F6A8885A308D3ull;
static unsigned rnd(void)
{
    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
    return (unsigned)(rs >> 32);
}

int main(void)
{
    HMODULE hm = GetModuleHandleW(L"ntdll.dll");
    live = (FFIND)GetProcAddress(hm, "RtlFindUnicodeSubstring");
    if (!live) { printf("RtlFindUnicodeSubstring not found\n"); return 1; }
    ref_init();

    printf("== CORRECTNESS: RtlFindUnicodeSubstring (ours vs oracle vs LIVE ntdll) ==\n");

    /* ---- 0. The assumption the vector filter rests on, checked on this machine's OS. ----
     * The insensitive filter compares each haystack unit against the anchor's case class, and is
     * exact only because that class never holds more than two members. That is a property of a
     * table this code does not own, so it is verified at run time rather than trusted: a Windows
     * that merged a third unit into a class would otherwise turn this into a search that silently
     * misses matches. */
    {
        int maxclass = wia_casemate_init();
        printf("  0. largest case-equivalence class in the OS upcase table: %d\n", maxclass);
        if (maxclass != 2) {
            printf("  FAIL: the vector filter assumes at most TWO members per class; this OS has "
                   "%d.\n        impl.asm's insensitive path is NOT valid here.\n", maxclass);
            ++fails;
        }
    }

    /* ---- 1. Exhaustive over a four-letter alphabet. Scalar tail only, see the header. ---- */
    {
        static const wchar_t A[4] = { L'a', L'A', L'b', L'B' };
        int hl, nl;
        long before = cases;
        for (hl = 0; hl <= 6; ++hl) {
            long hcount = 1, hi;
            for (nl = 0; nl < hl; ++nl) hcount *= 4;
            for (hi = 0; hi < hcount; ++hi) {
                wchar_t h[8];
                long t = hi;
                int k;
                for (k = 0; k < hl; ++k) { h[k] = A[t & 3]; t >>= 2; }
                for (nl = 0; nl <= 3; ++nl) {
                    long ncount = 1, ni;
                    for (k = 0; k < nl; ++k) ncount *= 4;
                    for (ni = 0; ni < ncount; ++ni) {
                        wchar_t nb[8];
                        long u = ni;
                        for (k = 0; k < nl; ++k) { nb[k] = A[u & 3]; u >>= 2; }
                        one(h, hl, nb, nl, 0, "exhaustive");
                        one(h, hl, nb, nl, 1, "exhaustive");
                    }
                }
            }
        }
        printf("  1. exhaustive {a,A,b,B}, haystack 0..6 x needle 0..3, both modes: %ld cases\n",
               cases - before);
    }

    /* ---- 2. The vector-loop boundary. The loop turns on at n-m >= 15, so walk across it. ---- */
    {
        long before = cases;
        int span, m, pos;
        for (span = 0; span <= 40; ++span) {          /* span = n - m */
            for (m = 1; m <= 20; ++m) {
                int n = span + m;
                int rep;
                if (n > 200) continue;
                for (rep = 0; rep < 3; ++rep) {
                    wchar_t h[256], nb[64];
                    int i;
                    for (i = 0; i < n; ++i) h[i] = (wchar_t)(L'a' + (rnd() % 3));
                    for (i = 0; i < m; ++i) nb[i] = (wchar_t)(L'a' + (rnd() % 3));
                    one(h, n, nb, m, 0, "boundary/random");
                    one(h, n, nb, m, 1, "boundary/random");
                    /* and the same needle PLANTED at every legal offset, which is what makes this
                       section find an off-by-one in the loop bound rather than only in the tail */
                    for (pos = 0; pos <= span; ++pos) {
                        for (i = 0; i < m; ++i) h[pos + i] = nb[i];
                        one(h, n, nb, m, 0, "boundary/planted");
                        one(h, n, nb, m, 1, "boundary/planted");
                        for (i = 0; i < m; ++i) h[pos + i] = (wchar_t)(L'a' + (rnd() % 3));
                    }
                }
            }
        }
        printf("  2. vector-loop boundary, n-m = 0..40 x m = 1..20, planted at every offset: "
               "%ld cases\n", cases - before);
    }

    /* ---- 3. RANDOMISED, long. Reaches the vector loop, over four alphabets. ---- */
    {
        long before = cases;
        int trial;
        static const wchar_t* ALPHA[4] = {
            L"ab",                                   /* two letters: many false anchors */
            L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ",
            L"\x00e0\x00c0\x00e8\x00c8\x017f\x0053\x0073\x03b1\x0391",  /* fold pairs, non-ASCII */
            L"abcXYZ\x00e0\x017f\x0410\x0430\xd800\xdc00"               /* mixed, incl. surrogates */
        };
        for (trial = 0; trial < 40000; ++trial) {
            wchar_t h[512], nb[64];
            const wchar_t* al = ALPHA[rnd() % 4];
            int alen = (int)wcslen(al);
            int n = 1 + (int)(rnd() % 300);
            int m = 1 + (int)(rnd() % 20);
            int i, ci = (int)(rnd() & 1);
            if (m > n) m = n;
            for (i = 0; i < n; ++i) h[i] = al[rnd() % alen];
            for (i = 0; i < m; ++i) nb[i] = al[rnd() % alen];
            if (rnd() & 1) {                          /* plant it, so hits are not all accidental */
                int pos = (int)(rnd() % (unsigned)(n - m + 1));
                for (i = 0; i < m; ++i) {
                    wchar_t c = nb[i];
                    /* plant a CASE-VARIED copy half the time, to exercise the insensitive path */
                    if ((rnd() & 1) && c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
                    else if ((rnd() & 1) && c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
                    h[pos + i] = c;
                }
            }
            one(h, n, nb, m, ci, "random/long");
        }
        printf("  3. randomised long strings, 4 alphabets, planted and absent: %ld cases\n",
               cases - before);
    }

    /* ---- 4. THE FOLD, at the pairs an ASCII fold cannot reach. ---- */
    {
        static const struct { wchar_t a, b; const char* what; } P[] = {
            { 0x00E0, 0x00C0, "a-grave / A-grave"                      },
            { 0x00FF, 0x0178, "y-diaeresis / Y-diaeresis"              },
            { 0x017F, 0x0053, "LONG S vs ASCII S -- the table does NOT merge these" },
            { 0x03B1, 0x0391, "greek alpha / ALPHA"                    },
            { 0x0430, 0x0410, "cyrillic a / A"                         },
            { 0x0131, 0x0049, "dotless i / ASCII 'I'"                  },
            { 0x0069, 0x0130, "ASCII 'i' / I-with-dot"                 },
            { 0x00DF, 0x1E9E, "sharp s / capital sharp s"              },
            { 0xFF41, 0xFF21, "fullwidth a / A"                        },
        };
        long before = cases;
        int i, k;
        for (i = 0; i < (int)(sizeof P / sizeof P[0]); ++i) {
            /* short (scalar tail) and long (vector loop), with the pair at several offsets */
            for (k = 0; k < 2; ++k) {
                int n = k ? 200 : 5;
                int pos;
                for (pos = 0; pos + 3 <= n; pos += (k ? 17 : 1)) {
                    wchar_t h[256], nb[8];
                    int j;
                    for (j = 0; j < n; ++j) h[j] = L'q';
                    h[pos] = L'x'; h[pos + 1] = P[i].a; h[pos + 2] = L'y';
                    nb[0] = L'x'; nb[1] = P[i].b; nb[2] = L'y';
                    one(h, n, nb, 3, 0, P[i].what);
                    one(h, n, nb, 3, 1, P[i].what);
                    /* and the pair as the needle's first and last character, i.e. as an anchor --
                       this is the case the vector filter has to be a superset for */
                    nb[0] = P[i].b; nb[1] = L'y'; nb[2] = P[i].b;
                    h[pos] = P[i].a; h[pos + 1] = L'y'; h[pos + 2] = P[i].a;
                    one(h, n, nb, 3, 0, P[i].what);
                    one(h, n, nb, 3, 1, P[i].what);
                    /* and as a single-character needle */
                    one(h, n, nb, 1, 1, P[i].what);
                }
            }
        }
        printf("  4. fold pairs, AND non-pairs the ordinal table refuses to merge, as body and as "
               "anchors: %ld cases\n",
               cases - before);
    }

    /* ---- 5. A GUARD PAGE. This is the claim "no page-safety clamp is needed", tested. ---- */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long before = cases, guard_cases = 0;
        int n, m;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
        if (!base) { printf("  5. VirtualAlloc failed -- guard-page test SKIPPED\n"); }
        else if (!VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  5. VirtualProtect failed -- guard-page test SKIPPED\n");
        } else {
            /* Put the haystack so its LAST character ends exactly at the page boundary: any read
               of even one character past Length touches PAGE_NOACCESS and raises. */
            for (n = 1; n <= 260; ++n) {
                wchar_t* h = (wchar_t*)(base + si.dwPageSize) - n;
                wchar_t nb[32];
                int i;
                for (i = 0; i < n; ++i) h[i] = (wchar_t)(L'a' + (i % 3));
                for (m = 1; m <= 20 && m <= n; ++m) {
                    for (i = 0; i < m; ++i) nb[i] = L'z';        /* absent: forces a FULL scan */
                    one(h, n, nb, m, 0, "guard-page/miss");
                    one(h, n, nb, m, 1, "guard-page/miss");
                    /* and a hit at the very LAST legal position, which is where the far anchor
                       reads closest to the boundary */
                    for (i = 0; i < m; ++i) nb[i] = h[n - m + i];
                    one(h, n, nb, m, 0, "guard-page/tail-hit");
                    one(h, n, nb, m, 1, "guard-page/tail-hit");
                    guard_cases += 4;
                }
            }
            printf("  5. guard page immediately after Length, n=1..260 x m=1..20: %ld cases, "
                   "no fault\n", guard_cases);
            VirtualFree(base, 0, MEM_RELEASE);
        }
        (void)before;
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf(fails ? "CORRECTNESS: FAILED\n" : "CORRECTNESS: PASS (bit-exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
