/* changes/177-pathisprefixw/correctness.c
 *
 * THREE-WAY: ours (change 167's walk behind a NULL check and a comparison), an independent oracle
 * (reference.c, which calls the LIVE PathCommonPrefixW so it isolates the envelope), and the LIVE
 * shlwapi!PathIsPrefixW.
 *
 * The observable is a BOOL, so the corpus has to carry the weight instead: the same exhaustive
 * 116281-pair set change 167 was parked on, a second alphabet that exercises the case-fold, the
 * shapes the original probes found surprising, and fuzz weighted so that half the pairs really are
 * prefixes -- because a corpus of random pairs is almost all FALSE, and an implementation that
 * returned FALSE unconditionally would pass it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern int  wia_pathisprefixw(const wchar_t*, const wchar_t*);
extern int  ref_pathisprefixw(const wchar_t*, const wchar_t*);
extern int  ref_init(void);
extern void wia_upcase_init(void);

typedef BOOL (WINAPI *FPIP)(const wchar_t*, const wchar_t*);
static FPIP sys;

static long cases = 0, fails = 0, ntrue = 0;

static void check(const wchar_t* pre, const wchar_t* path)
{
    int a, b, c;
    ++cases;
    a = wia_pathisprefixw(pre, path) != 0;
    b = ref_pathisprefixw(pre, path) != 0;
    c = sys(pre, path) != 0;
    if (a) ++ntrue;
    if (a != c || a != b) {
        if (++fails <= 20)
            printf("  MISMATCH pre=\"%ls\" path=\"%ls\"  ours=%d live=%d oracle=%d\n",
                   pre ? pre : L"(NULL)", path ? path : L"(NULL)", a, c, b);
    }
}

static uint64_t rs = 0xABCDEF0123456789ull;
static unsigned rng(void){ rs = rs*6364136223846793005ull + 1442695040888963407ull;
                           return (unsigned)(rs >> 33); }

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FPIP)GetProcAddress(h, "PathIsPrefixW");
    if (!sys) { printf("cannot resolve PathIsPrefixW\n"); return 1; }
    if (!ref_init()) { printf("oracle could not resolve PathCommonPrefixW\n"); return 1; }
    wia_upcase_init();

    printf("PathIsPrefixW: change 167's walk behind an envelope, vs an oracle, vs the LIVE export\n\n");

    /* ---- 1. the shapes the original probes found surprising ---- */
    {
        static const wchar_t* T[][2] = {
            { L"C:\\a", L"C:\\a\\b" }, { L"C:\\a\\", L"C:\\a\\b" }, { L"C:\\a", L"C:\\a" },
            { L"C:\\a", L"C:\\ab" }, { L"C:\\A", L"c:\\a\\b" }, { L"C:", L"C:\\a" },
            { L"C:\\", L"C:\\a" }, { L"\\\\srv\\s", L"\\\\srv\\s\\x" }, { L"\\\\srv", L"\\\\srv\\s" },
            { L"", L"C:\\a" }, { L"C:\\a", L"" }, { L"", L"" }, { L"\\", L"\\\\" },
            { L"\\\\", L"\\\\\\" }, { L"\\a", L"\\a\\" }, { L"a", L"a" }, { L"ab", L"ab\\c" },
            { L"\\\\?\\C:\\a", L"\\\\?\\C:\\a\\b" }, { L"C:/a", L"C:/a/b" },
        };
        int i;
        printf("1. THE SHAPES THE ORIGINAL PROBES FOUND ODD (a trailing '\\' in the prefix is FALSE)\n");
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            printf("   pre=%-12ls path=%-14ls -> %d\n", T[i][0], T[i][1],
                   wia_pathisprefixw(T[i][0], T[i][1]) != 0);
            check(T[i][0], T[i][1]);
        }
        printf("   %ld mismatches\n\n", fails);
    }

    /* ---- 2. the exhaustive corpus change 167 was parked on ---- */
    {
        static wchar_t buf[341][8];
        int cnt = 0, i, j, len;
        long mark = cases, f0 = fails, t0 = ntrue;
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
        printf("2. THE EXHAUSTIVE CORPUS -- all %d x %d = %d pairs over \"ab\\:\" to length 4\n",
               cnt, cnt, cnt * cnt);
        for (i = 0; i < cnt; ++i)
            for (j = 0; j < cnt; ++j)
                check(buf[i], buf[j]);
        printf("   %ld pairs, %ld mismatches, %ld of them TRUE\n\n",
               cases - mark, fails - f0, ntrue - t0);
    }

    /* ---- 3. a second alphabet, so the case-fold runs on every comparison ---- */
    {
        static wchar_t buf[400][8];
        int cnt = 0, i, j, len;
        long mark = cases, f0 = fails, t0 = ntrue;
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
        printf("3. A SECOND EXHAUSTIVE CORPUS over \"\\aA\" to length 4 -- case-folding throughout\n");
        for (i = 0; i < cnt; ++i)
            for (j = 0; j < cnt; ++j)
                check(buf[i], buf[j]);
        printf("   %ld pairs, %ld mismatches, %ld of them TRUE\n\n",
               cases - mark, fails - f0, ntrue - t0);
    }

    /* ---- 4. long paths: the prefix relationship at every length ---- */
    {
        long mark = cases, f0 = fails, t0 = ntrue;
        static wchar_t a[600], b[600];
        int n, k, cut;
        printf("4. LONG PATHS -- a 300-character path against its own prefix cut at every length,\n"
               "   with and without a trailing separator on the prefix\n");
        for (n = 0; n < 300; ++n) b[n] = (n % 7 == 6) ? L'\\' : (wchar_t)(L'a' + n % 26);
        b[300] = 0;
        for (cut = 0; cut <= 300; ++cut) {
            for (k = 0; k < cut; ++k) a[k] = b[k];
            a[cut] = 0;
            check(a, b);
            a[cut] = L'\\'; a[cut + 1] = 0;
            check(a, b);
            for (k = 0; k < cut; ++k) a[k] = (b[k] == L'\\') ? b[k] : (wchar_t)(b[k] - 32);
            a[cut] = 0;
            check(a, b);                      /* the same prefix, upper-cased */
        }
        printf("   %ld pairs, %ld mismatches, %ld of them TRUE\n\n",
               cases - mark, fails - f0, ntrue - t0);
    }

    /* ---- 5. fuzz, half of them genuine prefixes ---- */
    {
        long mark = cases, f0 = fails, t0 = ntrue;
        static wchar_t a[80], b[80];
        int t;
        printf("5. FUZZ: 300000 pairs, HALF of them genuine prefixes -- a corpus of random pairs is\n"
               "   almost all FALSE, and an implementation returning FALSE always would pass it\n");
        for (t = 0; t < 300000; ++t) {
            static const wchar_t A[] = L"ab\\:.ABC";
            int la = (int)(rng() % 20), lb, k;
            for (k = 0; k < la; ++k) a[k] = A[rng() % 8];
            a[la] = 0;
            if (rng() & 1) {
                lb = la + (int)(rng() % 12);
                for (k = 0; k < la; ++k) b[k] = a[k];
                for (k = la; k < lb; ++k) b[k] = A[rng() % 8];
            } else {
                lb = (int)(rng() % 24);
                for (k = 0; k < lb; ++k) b[k] = A[rng() % 8];
            }
            b[lb] = 0;
            check(a, b);
        }
        printf("   %ld pairs, %ld mismatches, %ld of them TRUE\n\n",
               cases - mark, fails - f0, ntrue - t0);
    }

    /* ---- 6. NULL, and a guard page ---- */
    {
        printf("6. NULL ARGUMENTS -- the one rule the identity cannot express, because wcslen(NULL)\n"
               "   is not evaluable\n");
        check(0, L"C:\\a");
        check(L"C:\\a", 0);
        check(0, 0);
        check(L"", 0);
        printf("   4 cases, %ld mismatches so far\n", fails);
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
                        __try { ra = wia_pathisprefixw(s, s) != 0; }
                        __except (EXCEPTION_EXECUTE_HANDLER) { fa = 1; }
                        __try { rc = sys(s, s) != 0; }
                        __except (EXCEPTION_EXECUTE_HANDLER) { fc = 1; }
                        ++cases;
                        if (fa || fc || ra != rc) { ++fails; ++bad; }
                    }
                    printf("   a path ending at a PAGE_NOACCESS boundary, 59 lengths: %s\n\n",
                           bad ? "MISMATCHES" : "never faults, identical to the live export");
                    VirtualFree(g, 0, MEM_RELEASE);
                }
            }
        }
    }

    printf("%ld cases, %ld mismatches (%ld returned TRUE) -- %s\n", cases, fails, ntrue,
           fails ? "CORRECTNESS FAILED" : "bit-exact");
    return fails ? 1 : 0;
}
