/* changes/167-pathcommonprefixw/correctness.c
 *
 * THREE-WAY: ours (a lockstep AVX2 walk), an independent oracle (reference.c, the literal
 * component-by-component loop the shipped function is written as), and the LIVE
 * shlwapi!PathCommonPrefixW.
 *
 * The two formulations are different on purpose. Agreement between them is evidence that the
 * equivalence argument in impl.asm's header is right, not evidence that one model was compiled
 * twice. The place it would break is the terminator rule, a NUL in one path against a '\' in the
 * other ends both components at the same length, so that component MATCHES, and section 2's
 * exhaustive corpus is saturated with exactly that shape.
 *
 * Both observables, always: the returned int and the whole achPath buffer against a 0xBEEF fill.
 * The buffer matters independently of the return, three times over: achPath is cleared even when the
 * answer is 0; a result of 3 can write only 2 characters, because the copy stops at pszFile1's own
 * terminator; and a result of 260 or more writes nothing at all.
 *
 * This change was parked at 99.3 %, so the exhaustive corpus below is the same 116281 pairs that
 * left 784 residuals, kept identical on purpose, because the point is that they are now zero.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern int wia_pathcommonprefixw(const wchar_t*, const wchar_t*, wchar_t*);
extern int ref_pathcommonprefixw(const wchar_t*, const wchar_t*, wchar_t*);
extern unsigned short wia_upcase[65536];
extern void wia_upcase_init(void);

typedef int (WINAPI *FPCP)(const wchar_t*, const wchar_t*, wchar_t*);
static FPCP sys;

#define BUF 320
static long cases = 0, fails = 0;

static void shows(const wchar_t* s)
{
    int i;
    if (!s) { printf("(NULL)"); return; }
    printf("\"");
    for (i = 0; s[i] && i < 40; ++i)
        printf((s[i] >= 32 && s[i] < 127) ? "%c" : "\\u%04X", (unsigned)s[i]);
    printf("\"");
}

static void check(const wchar_t* a, const wchar_t* b, int with_buf)
{
    wchar_t o1[BUF], o2[BUF], o3[BUF];
    int r1, r2, r3;
    int i, bad = 0;

    ++cases;
    for (i = 0; i < BUF; ++i) { o1[i] = 0xBEEF; o2[i] = 0xBEEF; o3[i] = 0xBEEF; }
    r1 = wia_pathcommonprefixw(a, b, with_buf ? o1 : 0);
    r2 = ref_pathcommonprefixw(a, b, with_buf ? o2 : 0);
    r3 = sys(a, b, with_buf ? o3 : 0);

    if (r1 != r3 || (with_buf && memcmp(o1, o3, sizeof o1) != 0)) bad = 1;
    else if (r1 != r2 || (with_buf && memcmp(o1, o2, sizeof o1) != 0)) bad = 2;

    if (bad) {
        if (++fails <= 20) {
            printf("  MISMATCH (%s) ", bad == 1 ? "vs LIVE" : "vs ORACLE (the two formulations)");
            shows(a); printf(" vs "); shows(b);
            printf("\n    ours %d ", r1);   if (with_buf) shows(o1);
            printf("\n    live %d ", r3);   if (with_buf) shows(o3);
            printf("\n    orcl %d ", r2);   if (with_buf) shows(o2);
            printf("\n");
        }
    }
}

static uint64_t rs = 0x5DEECE66Dull;
static unsigned rng(void){ rs = rs*6364136223846793005ull + 1442695040888963407ull;
                           return (unsigned)(rs >> 33); }

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FPCP)GetProcAddress(h, "PathCommonPrefixW");
    if (!sys) { printf("cannot resolve PathCommonPrefixW\n"); return 1; }
    wia_upcase_init();

    printf("PathCommonPrefixW: a lockstep AVX2 walk vs the component-loop oracle vs the LIVE export\n\n");

    /* ---- 1. the four pairs this change was parked on ---- */
    {
        static const struct { const wchar_t* a; const wchar_t* b; int was; } T[] = {
            { L"\\",   L"\\\\",   0 }, { L"\\\\", L"\\\\\\", 3 },
            { L"\\\\", L"\\\\a",  0 }, { L"\\a",  L"\\a\\",  3 },
        };
        int i;
        printf("1. THE FOUR PAIRS RESULTS.md CALLED MUTUALLY CONTRADICTORY\n");
        for (i = 0; i < 4; ++i) {
            wchar_t o[BUF];
            int r = wia_pathcommonprefixw(T[i].a, T[i].b, o);
            printf("   %-6ls vs %-6ls  ours=%d  live=%d  (recorded %d)\n",
                   T[i].a, T[i].b, r, sys(T[i].a, T[i].b, o), T[i].was);
            check(T[i].a, T[i].b, 1);
        }
        printf("   %ld mismatches\n\n", fails);
    }

    /* ---- 2. The exhaustive corpus: the very 116281 pairs that left 784 residuals ---- */
    {
        static wchar_t buf[341][8];
        int cnt = 0, i, j, len;
        long mark = cases, f0 = fails;
        for (len = 0; len <= 4; ++len) {
            long total = 1, v;
            for (i = 0; i < len; ++i) total *= 4;
            for (v = 0; v < total; ++v) {
                long t = v;
                for (i = 0; i < len; ++i) { buf[cnt][i] = L"ab\\:"[t % 4]; t /= 4; }
                buf[cnt][len] = 0;
                ++cnt;
            }
        }
        printf("2. THE EXHAUSTIVE CORPUS -- all %d x %d = %d pairs over \"ab\\:\" to length 4.\n"
               "   This is the same corpus that left 784 residuals when the rule was fitted from\n"
               "   outside; it is kept identical on purpose.\n", cnt, cnt, cnt * cnt);
        for (i = 0; i < cnt; ++i)
            for (j = 0; j < cnt; ++j)
                check(buf[i], buf[j], 1);
        printf("   %ld pairs, %ld mismatches\n\n", cases - mark, fails - f0);
    }

    /* ---- 3. a second exhaustive corpus, over a DIFFERENT alphabet ---- */
    {
        static wchar_t buf[400][8];
        int cnt = 0, i, j, len;
        long mark = cases, f0 = fails;
        for (len = 0; len <= 4; ++len) {
            long total = 1, v;
            for (i = 0; i < len; ++i) total *= 3;
            for (v = 0; v < total; ++v) {
                long t = v;
                for (i = 0; i < len; ++i) { buf[cnt][i] = L"\\aA"[t % 3]; t /= 3; }
                buf[cnt][len] = 0;
                ++cnt;
            }
        }
        printf("3. A SECOND EXHAUSTIVE CORPUS over \"\\aA\" to length 4 -- the same shapes with the\n"
               "   CASE-FOLD switched on, so every component comparison exercises the table\n");
        for (i = 0; i < cnt; ++i)
            for (j = 0; j < cnt; ++j)
                check(buf[i], buf[j], 1);
        printf("   %ld pairs, %ld mismatches\n\n", cases - mark, fails - f0);
    }

    /* ---- 4. the fold, over every code unit ---- */
    {
        long mark = cases, f0 = fails;
        unsigned u;
        wchar_t a[8], b[8];
        printf("4. THE CASE-FOLD over all 65534 code units, in a component\n");
        for (u = 1; u < 65536; ++u) {
            if (u == L'\\') continue;
            a[0] = L'\\'; a[1] = L'a'; a[2] = L'\\'; a[3] = (wchar_t)u; a[4] = 0;
            b[0] = L'\\'; b[1] = L'a'; b[2] = L'\\'; b[3] = (wchar_t)wia_upcase[u]; b[4] = 0;
            if (b[3] == L'\\' || b[3] == 0) continue;
            check(a, b, 1);
        }
        printf("   %ld pairs, %ld mismatches\n\n", cases - mark, fails - f0);
    }

    /* ---- 5. long paths: the vector loop, every alignment, and the 260 cap ---- */
    {
        long mark = cases, f0 = fails;
        static wchar_t a[600], b[600];
        int n, k, d;
        printf("5. LONG PATHS -- lengths 1..300 with the divergence walked along, so the vector\n"
               "   loop is entered, exited and resumed at every offset; plus the 260-character cap\n");
        for (n = 1; n <= 300; ++n) {
            for (k = 0; k < n; ++k) { a[k] = (k % 7 == 6) ? L'\\' : (wchar_t)(L'a' + k % 26);
                                      b[k] = a[k]; }
            a[n] = 0; b[n] = 0;
            check(a, b, 1);                                  /* identical */
            for (d = 0; d < n; d += (n > 40 ? 13 : 1)) {
                wchar_t save = b[d];
                b[d] = (save == L'\\') ? L'q' : L'\\';
                check(a, b, 1);
                b[d] = (wchar_t)(save ^ 0x20);               /* a case flip, not a difference */
                check(a, b, 1);
                b[d] = save;
            }
        }
        printf("   %ld pairs, %ld mismatches\n\n", cases - mark, fails - f0);
    }

    /* ---- 6. UNC and drive shapes, cross-product ---- */
    {
        static const wchar_t* T[] = {
            L"C:\\a\\b\\c", L"C:\\a\\b\\d", L"C:\\A\\B", L"c:\\a\\b", L"C:", L"C:\\", L"C:\\a",
            L"\\\\srv\\share\\x", L"\\\\SRV\\share\\y", L"\\\\srv\\other", L"\\\\srv",
            L"\\\\", L"\\\\\\", L"\\\\a", L"\\\\a\\", L"\\\\a\\b", L"\\", L"", L"a", L"ab",
            L"\\\\?\\C:\\a", L"\\\\?\\C:\\b", L"\\\\?\\UNC\\srv\\s", L"C:/a/b", L"C:/a/c",
            L"ab:\\x", L"ab:\\y", L"::", L"1:", L"x:", L"CD",
        };
        int n = (int)(sizeof T / sizeof T[0]), i, j;
        long mark = cases, f0 = fails;
        printf("6. UNC, EXTENDED-PREFIX AND DRIVE SHAPES -- all %d x %d pairs, with and without\n"
               "   an achPath buffer\n", n, n);
        for (i = 0; i < n; ++i)
            for (j = 0; j < n; ++j) { check(T[i], T[j], 1); check(T[i], T[j], 0); }
        printf("   %ld pairs, %ld mismatches\n\n", cases - mark, fails - f0);
    }

    /* ---- 7. fuzz ---- */
    {
        long mark = cases, f0 = fails;
        static wchar_t a[400], b[400];
        int t;
        printf("7. FUZZ: 200000 random path pairs over a path-shaped alphabet\n");
        for (t = 0; t < 200000; ++t) {
            int la = (int)(rng() % 40), lb, k, common;
            static const wchar_t A[] = L"ab\\:.ABC/ ";
            for (k = 0; k < la; ++k) a[k] = A[rng() % 10];
            a[la] = 0;
            common = (int)(rng() % (la + 1));
            lb = (int)(rng() % 40);
            for (k = 0; k < lb; ++k) b[k] = (k < common) ? a[k] : A[rng() % 10];
            b[lb] = 0;
            check(a, b, 1);
        }
        printf("   %ld pairs, %ld mismatches\n\n", cases - mark, fails - f0);
    }

    /* ---- 8. NULL arguments, and a guard page ---- */
    {
        wchar_t o[BUF];
        int i;
        printf("8. NULL ARGUMENTS (achPath must be left UNTOUCHED on those paths)\n");
        for (i = 0; i < BUF; ++i) o[i] = 0xBEEF;
        {
            int r1 = wia_pathcommonprefixw(0, L"C:\\a", o);
            unsigned u1 = o[0];
            int r3;
            for (i = 0; i < BUF; ++i) o[i] = 0xBEEF;
            r3 = sys(0, L"C:\\a", o);
            printf("   f1 NULL -> ours %d buf %04X, live %d buf %04X %s\n",
                   r1, u1, r3, o[0], (r1 == r3 && u1 == o[0]) ? "" : "<== MISMATCH");
            if (!(r1 == r3 && u1 == (unsigned)o[0])) ++fails;
            ++cases;
        }
        for (i = 0; i < BUF; ++i) o[i] = 0xBEEF;
        {
            int r1 = wia_pathcommonprefixw(L"C:\\a", 0, o);
            unsigned u1 = o[0];
            int r3;
            for (i = 0; i < BUF; ++i) o[i] = 0xBEEF;
            r3 = sys(L"C:\\a", 0, o);
            printf("   f2 NULL -> ours %d buf %04X, live %d buf %04X %s\n",
                   r1, u1, r3, o[0], (r1 == r3 && u1 == o[0]) ? "" : "<== MISMATCH");
            if (!(r1 == r3 && u1 == (unsigned)o[0])) ++fails;
            ++cases;
        }
        /* a path ending exactly at a PAGE_NOACCESS boundary: the 32-byte loads must not over-read */
        {
            SYSTEM_INFO si; GetSystemInfo(&si);
            {
                SIZE_T pg = si.dwPageSize;
                char* g = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                if (g) {
                    DWORD old;
                    int tail, bad = 0;
                    VirtualProtect(g + pg, pg, PAGE_NOACCESS, &old);
                    for (tail = 2; tail <= 60; ++tail) {
                        wchar_t* s = (wchar_t*)((g + pg) - tail * 2);
                        int k, fa = 0, fc = 0, ra = 0, rc = 0;
                        for (k = 0; k < tail - 1; ++k)
                            s[k] = (k % 5 == 4) ? L'\\' : (wchar_t)(L'a' + k % 26);
                        s[tail - 1] = 0;
                        __try { ra = wia_pathcommonprefixw(s, s, o); }
                        __except (EXCEPTION_EXECUTE_HANDLER) { fa = 1; }
                        __try { rc = sys(s, s, o); }
                        __except (EXCEPTION_EXECUTE_HANDLER) { fc = 1; }
                        ++cases;
                        if (fa || fc || ra != rc) { ++fails; ++bad;
                            if (bad <= 5) printf("   guard tail=%d ours %s %d, live %s %d\n", tail,
                                                 fa ? "FAULTED" : "ok", ra,
                                                 fc ? "FAULTED" : "ok", rc); }
                    }
                    printf("   a path ending at a PAGE_NOACCESS boundary, 59 lengths: %s\n",
                           bad ? "MISMATCHES ABOVE" : "never faults, identical to the live export");
                    VirtualFree(g, 0, MEM_RELEASE);
                }
            }
        }
        printf("\n");
    }

    printf("%ld cases, %ld mismatches -- %s\n", cases, fails,
           fails ? "CORRECTNESS FAILED" : "bit-exact");
    return fails ? 1 : 0;
}
