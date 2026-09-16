/* changes/177-pathisprefixw/probes/pip2.c
 *
 * RE-CONFIRMING THE IDENTITY, now that the thing it was blocked on has landed.
 *
 * This change was parked with an unusually strong note: not "we could not derive it" but
 *
 *     PathIsPrefixW(pre, path)  ==  ( PathCommonPrefixW(path, pre, NULL) == wcslen(pre) )
 *
 * "fuzz-verified against BOTH live exports (2M cases, 0 mismatches)" -- and blocked, because
 * PathCommonPrefixW was itself parked at 99.3 %. Change 167 has now landed bit-exact, so the
 * blockage is gone and this function is a one-line composition over it.
 *
 * A RECORDED CLAIM IS NOT A MEASUREMENT, so this re-establishes the identity from scratch against
 * the two live exports rather than taking the old note's word for it, and pins the one thing the
 * identity cannot settle on its own: what happens when either argument is NULL, since wcslen(NULL)
 * is not something the right-hand side can evaluate.
 *
 * It also checks the direction of the arguments explicitly. The identity swaps them --
 * PathIsPrefixW(pre, path) consults PathCommonPrefixW(path, pre, ...) -- and getting that backwards
 * would still pass on every symmetric case, which is most short random pairs.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef BOOL (WINAPI *FPIP)(const wchar_t*, const wchar_t*);
typedef int  (WINAPI *FPCP)(const wchar_t*, const wchar_t*, wchar_t*);
static FPIP pip;
static FPCP pcp;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 30) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

static long cases = 0, bad = 0, bad_rev = 0;

static void one(const wchar_t* pre, const wchar_t* path)
{
    BOOL live = pip(pre, path);
    int  cp   = pcp(path, pre, 0);
    BOOL model = (cp == (int)wcslen(pre));
    /* the same identity with the arguments the OTHER way round, to show the order matters */
    int  cprev = pcp(pre, path, 0);
    BOOL modelrev = (cprev == (int)wcslen(pre));

    ++cases;
    if ((live != 0) != (model != 0)) {
        ++bad;
        if (bad <= 12)
            printf("  MISMATCH pre=\"%ls\" path=\"%ls\"  live=%d  cp(path,pre)=%d wcslen(pre)=%d\n",
                   pre, path, (int)live, cp, (int)wcslen(pre));
    }
    if ((live != 0) != (modelrev != 0)) ++bad_rev;
}

static uint64_t rs = 0x1234567890ABCDEFull;
static unsigned rng(void){ rs = rs*6364136223846793005ull + 1442695040888963407ull;
                           return (unsigned)(rs >> 33); }

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    pip = (FPIP)GetProcAddress(h, "PathIsPrefixW");
    pcp = (FPCP)GetProcAddress(h, "PathCommonPrefixW");
    if (!pip || !pcp) { printf("cannot resolve PathIsPrefixW / PathCommonPrefixW\n"); return 1; }
    printf("PathIsPrefixW  ==  PathCommonPrefixW(path, pre, NULL) == wcslen(pre)?\n\n");

    /* ---- 1. the pinned shapes, including the ones the old probes found odd ---- */
    {
        static const wchar_t* T[][2] = {
            { L"C:\\a",      L"C:\\a\\b"     },
            { L"C:\\a\\",    L"C:\\a\\b"     },   /* a trailing '\' in the prefix -> FALSE */
            { L"C:\\a",      L"C:\\a"        },
            { L"C:\\a",      L"C:\\ab"       },
            { L"C:\\A",      L"c:\\a\\b"     },
            { L"C:",         L"C:\\a"        },
            { L"C:\\",       L"C:\\a"        },
            { L"\\\\srv\\s", L"\\\\srv\\s\\x"},
            { L"\\\\srv",    L"\\\\srv\\s"   },
            { L"",           L"C:\\a"        },
            { L"C:\\a",      L""             },
            { L"",           L""             },
            { L"\\",         L"\\\\"         },
            { L"\\\\",       L"\\\\\\"       },
            { L"\\a",        L"\\a\\"        },
            { L"a",          L"a"            },
            { L"ab",         L"ab\\c"        },
        };
        int i;
        printf("1. PINNED SHAPES\n");
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            BOOL live = pip(T[i][0], T[i][1]);
            int cp = pcp(T[i][1], T[i][0], 0);
            printf("   pre=%-12ls path=%-14ls live=%d   cp(path,pre)=%-3d wcslen(pre)=%-3d -> %d\n",
                   T[i][0], T[i][1], (int)(live != 0), cp, (int)wcslen(T[i][0]),
                   cp == (int)wcslen(T[i][0]));
            one(T[i][0], T[i][1]);
        }
        printf("   %ld mismatches\n\n", bad);
    }

    /* ---- 2. the exhaustive corpus: every pair over {a, b, \, :} to length 4 ---- */
    {
        static wchar_t buf[341][8];
        int cnt = 0, i, j, len;
        long mark = cases, b0 = bad, r0 = bad_rev;
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
                one(buf[i], buf[j]);
        printf("   %ld pairs, %ld mismatches\n", cases - mark, bad - b0);
        printf("   with the two arguments SWAPPED in the model: %ld mismatches -- PathCommonPrefixW\n"
               "   is SYMMETRIC when achPath is NULL, so the order in the identity does not matter\n\n",
               bad_rev - r0);
        CHECK(bad - b0 == 0, "%ld exhaustive pairs break the identity", bad - b0);
        /* THE SWAPPED MODEL AGREES EVERYWHERE TOO, and that is a finding rather than a failure.
           With achPath NULL, PathCommonPrefixW is SYMMETRIC in its first two arguments -- it has to
           be: the components are compared symmetrically, the UNC skip applies to both or to neither,
           and the matched prefix has the same LENGTH measured from either side, so the only thing
           that could distinguish them is the copy-out, which is not happening here. The argument
           order in the recorded identity is a red herring, and this check now asserts the symmetry
           rather than asserting a difference that does not exist. */
        CHECK(bad_rev - r0 == 0, "PathCommonPrefixW is NOT symmetric with achPath NULL: %ld pairs",
              bad_rev - r0);
    }

    /* ---- 3. realistic paths and case folding ---- */
    {
        static const wchar_t* T[] = {
            L"C:\\Program Files", L"C:\\Program Files\\App", L"c:\\program files\\app",
            L"C:\\Program", L"\\\\srv\\share", L"\\\\srv\\share\\dir\\file.txt",
            L"\\\\SRV\\SHARE", L"D:\\", L"D:", L"D:\\x", L"\\\\?\\C:\\a", L"\\\\?\\C:\\a\\b",
        };
        int n = (int)(sizeof T / sizeof T[0]), i, j;
        long mark = cases, b0 = bad;
        printf("3. REALISTIC PATHS, all %d x %d pairs\n", n, n);
        for (i = 0; i < n; ++i) for (j = 0; j < n; ++j) one(T[i], T[j]);
        printf("   %ld pairs, %ld mismatches\n\n", cases - mark, bad - b0);
        CHECK(bad - b0 == 0, "%ld realistic pairs break the identity", bad - b0);
    }

    /* ---- 4. fuzz, weighted so that `pre` really is a prefix of `path` about half the time ---- */
    {
        long mark = cases, b0 = bad;
        static wchar_t a[80], b[80];
        int t;
        printf("4. FUZZ: 400000 pairs, half of them genuine prefixes\n");
        for (t = 0; t < 400000; ++t) {
            static const wchar_t A[] = L"ab\\:.ABC";
            int la = (int)(rng() % 20), lb, k;
            for (k = 0; k < la; ++k) a[k] = A[rng() % 8];
            a[la] = 0;
            if (rng() & 1) {                       /* build path as an extension of pre */
                lb = la + (int)(rng() % 12);
                for (k = 0; k < la; ++k) b[k] = a[k];
                for (k = la; k < lb; ++k) b[k] = A[rng() % 8];
            } else {
                lb = (int)(rng() % 24);
                for (k = 0; k < lb; ++k) b[k] = A[rng() % 8];
            }
            b[lb] = 0;
            one(a, b);
        }
        printf("   %ld pairs, %ld mismatches\n\n", cases - mark, bad - b0);
        CHECK(bad - b0 == 0, "%ld fuzz pairs break the identity", bad - b0);
    }

    /* ---- 5. NULL: the one thing the identity cannot evaluate ---- */
    {
        printf("5. NULL ARGUMENTS -- wcslen(NULL) is not something the right-hand side can do,\n"
               "   so the envelope has to carry this rule itself\n");
        printf("   pre NULL, path \"C:\\\\a\"  -> %d\n", (int)(pip(0, L"C:\\a") != 0));
        printf("   pre \"C:\\\\a\", path NULL  -> %d\n", (int)(pip(L"C:\\a", 0) != 0));
        printf("   both NULL               -> %d\n", (int)(pip(0, 0) != 0));
        printf("   pre \"\", path NULL       -> %d\n\n", (int)(pip(L"", 0) != 0));
    }

    printf("%ld cases, %ld mismatches -- %s\n", cases, bad,
           bad ? "THE IDENTITY IS WRONG" : "the identity holds");
    return (fails || bad) ? 1 : 0;
}
