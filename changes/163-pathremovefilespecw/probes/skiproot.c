/* Deriving PathCchSkipRoot -- the keystone under PathSkipRootW, PathIsSameRootW and
 * PathRemoveFileSpecW.
 *
 * The disassembly says:
 *   PathSkipRootW(p)      = PathCchSkipRoot(p, &end); if (hr < 0) return end(NULL);
 *                           if (end == p+2 && p[1] == ':') end = NULL; return end;
 *   PathIsSameRootW(a, b) = a && b && PathSkipRootW(a) != NULL
 *                           && (PathSkipRootW(a) - a) <= PathCommonPrefixW(a, b, NULL) + 1
 *
 * so everything reduces to PathCchSkipRoot. This dumps its answer over an exhaustive corpus so the
 * rule can be read off rather than guessed, the way change 167's pcp4.c was used.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *FSKIP)(const wchar_t*, const wchar_t**);
typedef wchar_t* (WINAPI *FSKW)(const wchar_t*);
static FSKIP skip;
static FSKW  skw;

static void show(const wchar_t* s)
{
    int i;
    printf("\"");
    for (i = 0; s[i]; ++i) printf("%c", (s[i] >= 32 && s[i] < 127) ? (char)s[i] : '?');
    printf("\"");
}

/* returns the root length in characters, or -1 for an error, -2 for a NULL end */
static int rootlen(const wchar_t* s, HRESULT* hr_out)
{
    const wchar_t* e = 0;
    HRESULT hr = skip(s, &e);
    if (hr_out) *hr_out = hr;
    if (hr < 0) return -1;
    if (!e) return -2;
    return (int)(e - s);
}

int main(void)
{
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    skip = (FSKIP)GetProcAddress(hk, "PathCchSkipRoot");
    skw  = (FSKW) GetProcAddress(hs, "PathSkipRootW");
    if (!skip || !skw) { printf("resolve failed\n"); return 1; }

    /* ---- 1. the documented shapes, so the vocabulary is clear ---- */
    {
        static const wchar_t* T[] = {
            L"", L"a", L"ab", L"a\\b", L"\\", L"\\\\", L"\\\\\\", L"\\a", L"\\a\\", L"\\a\\b",
            L"C:", L"C:\\", L"C:\\a", L"C:a", L"C:\\\\", L"1:\\", L"::\\", L"C:/", L"C:/a",
            L"\\\\s", L"\\\\s\\", L"\\\\s\\h", L"\\\\s\\h\\", L"\\\\s\\h\\a", L"\\\\s\\\\h",
            L"\\\\?\\", L"\\\\?\\C:", L"\\\\?\\C:\\", L"\\\\?\\C:\\a", L"\\\\?\\a",
            L"\\\\?\\UNC", L"\\\\?\\UNC\\", L"\\\\?\\UNC\\s", L"\\\\?\\UNC\\s\\",
            L"\\\\?\\UNC\\s\\h", L"\\\\?\\UNC\\s\\h\\", L"\\\\?\\UNC\\s\\h\\a",
            L"\\\\?\\unc\\s\\h\\", L"\\\\.\\C:\\", L"\\\\.\\PhysicalDrive0",
            L"\\\\?\\Volume{12345678-1234-1234-1234-123456789abc}\\",
            L"//s/h/", L"\\/s\\h", L"\\\\?/C:\\",
        };
        int i;
        printf("1. PathCchSkipRoot, and PathSkipRootW alongside it\n");
        printf("   %-46s %8s %6s %8s\n", "path", "hr", "rootlen", "SkipRootW");
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            HRESULT hr;
            int n = rootlen(T[i], &hr);
            wchar_t* w = skw(T[i]);
            printf("   %-46ls %08lX %6d %8s\n", T[i], (unsigned long)hr, n,
                   w ? "p+?" : "NULL");
            if (w) printf("%*s(SkipRootW = p+%d)\n", 66, "", (int)(w - T[i]));
        }
        printf("\n");
    }

    /* ---- 2. the exhaustive dump: every string over {a, \, :, ?} to length 5 whose
              root length is NOT simply reproducible, printed so the rule can be read ---- */
    {
        static const wchar_t A[] = L"a\\:?";
        static wchar_t s[8];
        int len;
        long total_ok = 0, total_err = 0;
        printf("2. EXHAUSTIVE over \"a\\:?\" to length 5 -- grouped by (hr, rootlen)\n");
        /* count the distinct answers first */
        {
            long hist[12] = {0};
            for (len = 0; len <= 5; ++len) {
                long tot = 1, v;
                int i;
                for (i = 0; i < len; ++i) tot *= 4;
                for (v = 0; v < tot; ++v) {
                    long t = v;
                    HRESULT hr;
                    int n;
                    for (i = 0; i < len; ++i) { s[i] = A[t % 4]; t /= 4; }
                    s[len] = 0;
                    n = rootlen(s, &hr);
                    if (n < 0) { ++total_err; ++hist[0]; }
                    else { ++total_ok; if (n < 11) ++hist[n + 1]; }
                }
            }
            printf("   %ld succeeded, %ld returned an error\n", total_ok, total_err);
            printf("   rootlen histogram:");
            { int i; for (i = 0; i < 11; ++i) if (hist[i]) printf(" [%s]=%ld",
                       i == 0 ? "err" : (i == 1 ? "0" : (i == 2 ? "1" : (i == 3 ? "2" :
                       (i == 4 ? "3" : (i == 5 ? "4" : (i == 6 ? "5" : (i == 7 ? "6" :
                       (i == 8 ? "7" : (i == 9 ? "8" : "9"))))))))), hist[i]); }
            printf("\n\n");
        }
    }

    /* ---- 3. leading-backslash runs, which is where 163's note said it was non-monotonic ---- */
    {
        wchar_t s[40];
        int k, j;
        printf("3. LEADING BACKSLASH RUNS (163's note: \"k=3->3, k=4->2, so no single rule fits\")\n");
        for (k = 1; k <= 8; ++k) {
            HRESULT hr;
            int n;
            for (j = 0; j < k; ++j) s[j] = L'\\';
            s[k] = 0;
            n = rootlen(s, &hr);
            printf("   %d backslashes                -> hr=%08lX rootlen=%d\n",
                   k, (unsigned long)hr, n);
            for (j = 0; j < k; ++j) s[j] = L'\\';
            s[k] = L'a'; s[k+1] = 0;
            n = rootlen(s, &hr);
            printf("   %d backslashes then 'a'       -> hr=%08lX rootlen=%d\n",
                   k, (unsigned long)hr, n);
            for (j = 0; j < k; ++j) s[j] = L'\\';
            s[k] = L'a'; s[k+1] = L'\\'; s[k+2] = L'b'; s[k+3] = 0;
            n = rootlen(s, &hr);
            printf("   %d backslashes then 'a\\b'     -> hr=%08lX rootlen=%d\n",
                   k, (unsigned long)hr, n);
        }
        printf("\n");
    }

    /* ---- 4. is PathSkipRootW exactly "PathCchSkipRoot, but NULL when the root is C:"? ---- */
    {
        static const wchar_t A[] = L"a\\:?";
        static wchar_t s[8];
        int len, bad = 0;
        long n = 0;
        printf("4. PathSkipRootW == PathCchSkipRoot with \"root of exactly 2 ending in ':' -> NULL\"?\n");
        for (len = 0; len <= 5; ++len) {
            long tot = 1, v;
            int i;
            for (i = 0; i < len; ++i) tot *= 4;
            for (v = 0; v < tot; ++v) {
                long t = v;
                const wchar_t* e = 0;
                HRESULT hr;
                wchar_t* w;
                const wchar_t* model;
                for (i = 0; i < len; ++i) { s[i] = A[t % 4]; t /= 4; }
                s[len] = 0;
                hr = skip(s, &e);
                model = (hr < 0) ? 0 : e;
                if (model && model == s + 2 && s[1] == L':') model = 0;
                w = skw(s);
                ++n;
                if (w != model) {
                    ++bad;
                    if (bad <= 10) {
                        printf("   DIFFER ");
                        show(s);
                        printf("  live=%s model=%s (hr=%08lX end=p+%d)\n",
                               w ? "p+n" : "NULL", model ? "p+n" : "NULL",
                               (unsigned long)hr, e ? (int)(e - s) : -1);
                    }
                }
            }
        }
        printf("   %ld strings, %d disagreements\n\n", n, bad);
    }

    return 0;
}
