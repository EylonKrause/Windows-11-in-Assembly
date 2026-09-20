/* changes/167-pathcommonprefixw/probes/pcp6.c
 *
 * The rule, read out of the binary -- and tested against the live export over the same exhaustive
 * corpus that left the black-box derivation at 99.3 %.
 *
 * This change was parked because 784 of 116281 exhaustive pairs over {a, b, \, :} resisted every
 * rule that could be fitted from the outside, and because RESULTS.md concluded:
 *
 *     "Reproducing PathCommonPrefixW bit-exactly requires reproducing that root parser first"
 *     "Next step if resumed: derive PathSkipRootW first ... it very likely underlies
 *      PathCommonPrefixW, PathIsPrefixW and PathIsSameRootW alike"
 *
 * That hypothesis is wrong, and the disassembly says so in one line: kernelbase!PathCommonPrefixW
 * (Rva 0x0CBD10) never calls PathSkipRootW, or any root parser at all. Its entire root handling is
 * two inline tests for a doubled leading backslash:
 *
 *     000CBD43  cmp word ptr [rcx], 0x5c    / je 0x0CBE2C      f1 starts with '\'
 *     000CBE2C  cmp word ptr [rcx+2], 0x5c  / jne 0x0CBD50     ... and a second one?
 *     000CBE3A  call 0x0CBEA4                                  is_unc(f2)?
 *     000CBE43  lea r8, [r14 + 4]                              then skip TWO characters of f1
 *     (and the mirror image for f2 at 0x0CBD50 / 0x0CBE4C / 0x0CBE63)
 *
 *     000CBEA4  is_unc(p):  return p[0] == '\\' && p[1] == '\\'
 *
 * and the anomaly that produced the "+1 only when the separator sits at index 2 with s[1]==':'"
 * guess is not about colons at all. It is three instructions:
 *
 *     000CBDEA  sub rsi, r14        ; boundary - pszFile1, in BYTES
 *     000CBDED  mov ebp, 3
 *     000CBDF2  sar rsi, 1          ; -> characters
 *     000CBDF5  cmp esi, 2
 *     000CBDF8  cmovne ebp, esi     ; if the length is exactly 2, the answer is 3
 *
 * ANY computed prefix length of two becomes three. Not "two identical 2-character strings", and
 * nothing to do with 'C' or ':'.
 *
 * Those two facts settle all four of the residuals RESULTS.md lists as mutually contradictory:
 *
 *   "\"  vs "\\"   -> f2 is UNC, f1 is not          -> 0     (the is_unc(f1) test fails)
 *   "\\" vs "\\\"  -> both UNC, both skip 2, both components empty, boundary = f1+2 -> n=2 -> 3
 *   "\\" vs "\\a"  -> both UNC, lengths 0 and 1     -> 0     (no boundary ever set)
 *   "\a" vs "\a\"  -> neither is UNC, two components match, boundary = f1+2 -> n=2 -> 3
 *
 * This program encodes the rule as read and refutes it the only way that counts: against the live
 * export, over the full exhaustive corpus plus the shapes that matter.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *FPCP)(const wchar_t*, const wchar_t*, wchar_t*);
static FPCP sys;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 30) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* The case-fold: change 167's go/no-go established it is exactly CharUpperW / RtlUpcaseUnicodeChar,
   0 differences over all 65534 code-unit pairs, against 947 for a plain ASCII fold. */
static wchar_t up(wchar_t c)
{
    return (wchar_t)(unsigned __int64)CharUpperW((LPWSTR)(unsigned __int64)(unsigned)c);
}

static int is_unc(const wchar_t* p)
{
    return p[0] == L'\\' && p[1] == L'\\';
}

/* The model, transcribed from the disassembly and from nothing else. */
static int model(const wchar_t* f1, const wchar_t* f2, wchar_t* out)
{
    const wchar_t *p1, *p2, *boundary = 0;
    int ret = 0;

    if (!f1 || !f2) return 0;              /* and `out` is NOT written on this path */
    if (out) *out = 0;                     /* 0x0CBE70: cleared before anything else */

    p1 = f1; p2 = f2;
    if (f1[0] == L'\\' && f1[1] == L'\\') {
        if (!is_unc(f2)) return 0;
        p1 = f1 + 2;
    }
    if (f2[0] == L'\\' && f2[1] == L'\\') {
        if (!is_unc(f1)) return 0;
        p2 = f2 + 2;
    }

    for (;;) {
        const wchar_t *e1 = p1, *e2 = p2;
        size_t l1, l2, k;
        int eq = 1;
        while (*e1 && *e1 != L'\\') ++e1;
        while (*e2 && *e2 != L'\\') ++e2;
        l1 = (size_t)(e1 - p1);
        l2 = (size_t)(e2 - p2);
        if (l1 != l2) break;
        for (k = 0; k < l1; ++k) if (up(p1[k]) != up(p2[k])) { eq = 0; break; }
        if (!eq) break;
        boundary = e1;
        if (*e1 == 0) break;
        p1 = e1 + 1;
        if (*e2 == 0) break;
        p2 = e2 + 1;
    }

    if (boundary) {
        int n = (int)(boundary - f1);
        ret = (n == 2) ? 3 : n;            /* 0x0CBDF5: the whole of the "quirk" */
    }
    if (out && ret < 260) {
        /* StringCchCopyN(out, 260, f1, ret) -- stops at f1's own terminator, which is why a
           result of 3 can still write only 2 characters */
        int k = 0;
        while (k < ret && f1[k]) { out[k] = f1[k]; ++k; }
        out[k] = 0;
    }
    return ret;
}

static long cases = 0, bad = 0, bad_ret = 0, bad_buf = 0;

static void one(const wchar_t* a, const wchar_t* b)
{
    wchar_t o1[300], o2[300];
    int r1, r2;
    ++cases;
    for (int i = 0; i < 300; ++i) { o1[i] = 0xBEEF; o2[i] = 0xBEEF; }
    r1 = model(a, b, o1);
    r2 = sys(a, b, o2);
    if (r1 != r2) { ++bad; ++bad_ret; }
    else if (memcmp(o1, o2, sizeof o1) != 0) { ++bad; ++bad_buf; }
    else return;
    if (bad <= 20)
        printf("  MISMATCH \"%ls\" vs \"%ls\"  model=%d live=%d   model buf=\"%ls\" live buf=\"%ls\"\n",
               a, b, r1, r2, o1, o2);
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FPCP)GetProcAddress(h, "PathCommonPrefixW");
    if (!sys) { printf("cannot resolve PathCommonPrefixW\n"); return 1; }
    printf("PathCommonPrefixW -- the rule read out of the binary, refuted against the live export\n\n");

    /* --- 1. the four residuals RESULTS.md calls mutually contradictory --- */
    {
        static const struct { const wchar_t* a; const wchar_t* b; int live; } T[] = {
            { L"\\",   L"\\\\",    0 },
            { L"\\\\", L"\\\\\\",  3 },
            { L"\\\\", L"\\\\a",   0 },
            { L"\\a",  L"\\a\\",   3 },
        };
        printf("1. THE FOUR RESIDUALS THAT PARKED THIS CHANGE\n");
        for (int i = 0; i < 4; ++i) {
            wchar_t o[300];
            int m = model(T[i].a, T[i].b, o);
            int l = sys(T[i].a, T[i].b, o);
            printf("   %-8ls vs %-8ls  model=%d  live=%d  (RESULTS.md recorded %d)  %s\n",
                   T[i].a, T[i].b, m, l, T[i].live, m == l ? "" : "<== STILL WRONG");
            CHECK(m == l, "residual %d: model %d, live %d", i, m, l);
        }
        printf("\n");
    }

    /* --- 2. The exhaustive corpus: all pairs over {a, b, \, :} to length 4 --- */
    {
        static const wchar_t A[] = L"ab\\:";
        static wchar_t s[8][400];
        int n = 0;
        long mark;
        for (int len = 0; len <= 4; ++len) {
            long total = 1;
            for (int i = 0; i < len; ++i) total *= 4;
            for (long v = 0; v < total; ++v) { (void)v; }
            (void)total;
        }
        (void)s; (void)n;
        /* build the list */
        {
            static wchar_t buf[341][8];
            int cnt = 0;
            for (int len = 0; len <= 4; ++len) {
                long total = 1;
                for (int i = 0; i < len; ++i) total *= 4;
                for (long v = 0; v < total; ++v) {
                    long t = v;
                    for (int i = 0; i < len; ++i) { buf[cnt][i] = A[t % 4]; t /= 4; }
                    buf[cnt][len] = 0;
                    ++cnt;
                }
            }
            printf("2. THE EXHAUSTIVE CORPUS -- all %d x %d = %d pairs over \"%ls\" to length 4\n",
                   cnt, cnt, cnt * cnt, A);
            mark = cases;
            for (int i = 0; i < cnt; ++i)
                for (int j = 0; j < cnt; ++j)
                    one(buf[i], buf[j]);
            printf("   %ld pairs, %ld mismatches (%ld on the return, %ld on the buffer)\n\n",
                   cases - mark, bad, bad_ret, bad_buf);
        }
    }

    /* --- 3. realistic paths, UNC, drive roots, and the case-fold --- */
    {
        long mark = cases, b0 = bad;
        static const wchar_t* T[] = {
            L"C:\\a\\b\\c", L"C:\\a\\b\\d", L"C:\\a\\bb", L"C:\\A\\B\\C", L"c:\\a\\b\\c",
            L"\\\\srv\\share\\x", L"\\\\srv\\share\\y", L"\\\\SRV\\SHARE\\x", L"\\\\srv\\other",
            L"\\\\?\\C:\\a", L"\\\\?\\C:\\b", L"C:", L"C:\\", L"CD", L"ab", L"x:", L"1:", L"::",
            L"C:/abc/def", L"C:/abc/dxx", L"a", L"", L"\\", L"\\\\", L"\\\\\\", L"\\\\\\\\",
            L"\\a", L"\\a\\", L"\\\\a", L"\\\\a\\", L"\\\\a\\b", L"\\\\a\\b\\c",
            L"C:\\abc", L"C:\\abcd", L"ab:\\x", L"ab:\\y",
        };
        int n = (int)(sizeof T / sizeof T[0]);
        printf("3. REALISTIC PATHS, UNC, DRIVE ROOTS AND THE CASE-FOLD -- all %d x %d pairs\n", n, n);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                one(T[i], T[j]);
        printf("   %ld pairs, %ld mismatches\n\n", cases - mark, bad - b0);
    }

    /* --- 4. the case-fold over every code unit, in a component --- */
    {
        long mark = cases, b0 = bad;
        wchar_t a[8], b[8];
        printf("4. THE CASE-FOLD: \"\\a\\<u>\" vs \"\\a\\<upper(u)>\" for every code unit\n");
        for (unsigned u = 1; u < 65536; ++u) {
            if (u == L'\\') continue;
            a[0] = L'\\'; a[1] = L'a'; a[2] = L'\\'; a[3] = (wchar_t)u; a[4] = 0;
            b[0] = L'\\'; b[1] = L'a'; b[2] = L'\\'; b[3] = up((wchar_t)u); b[4] = 0;
            if (b[3] == L'\\') continue;
            one(a, b);
        }
        printf("   %ld pairs, %ld mismatches\n\n", cases - mark, bad - b0);
    }

    /* --- 5. NULL arguments, and whether achPath is written --- */
    {
        wchar_t o[300];
        printf("5. NULL ARGUMENTS\n");
        for (int i = 0; i < 300; ++i) o[i] = 0xBEEF;
        printf("   f1 NULL -> live %d, buf[0]=%04X (0xBEEF = untouched)\n",
               sys(0, L"C:\\a", o), o[0]);
        for (int i = 0; i < 300; ++i) o[i] = 0xBEEF;
        printf("   f2 NULL -> live %d, buf[0]=%04X\n", sys(L"C:\\a", 0, o), o[0]);
        printf("   achPath NULL, no match -> live %d\n", sys(L"a", L"b", 0));
        printf("   achPath NULL, match    -> live %d\n\n", sys(L"C:\\a", L"C:\\a", 0));
    }

    printf("%ld cases, %ld mismatches -- %s\n", cases, bad,
           bad ? "THE MODEL IS STILL WRONG" : "the model read from the binary REPRODUCES the export");
    return (fails || bad) ? 1 : 0;
}
