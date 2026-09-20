/* discovery/wide_siblings_left_behind.c
 *
 * A PATTERN, not a hunch. Twice now, a wide CRT export has turned out to be scalar while its narrow
 * sibling was vectorised:
 *
 *     change 149   ucrtbase!wcsrchr    0.117 ns/char, scalar    strrchr  11.8 ns / 254 chars, SSE4.2
 *     change 148   ucrtbase!wcstok_s   1.45 ns/char             strtok_s 0.93 ns/char
 *
 * 149 is the interesting one, because image/KEEP-AS-IS.md had recorded
 *
 *     | `strrchr` / `wcsrchr` | SSE4.2 pcmpistri | already vectorized |
 *
 *, one row, two exports, on the assumption that a narrow function and its wide sibling share an
 * implementation. They did not, and the register was wrong for a year until a change measured it.
 *
 * tools/keepasis-audit.py now lists every row in that register that names more than one export.
 * Three of the remaining four pair a narrow with a wide:
 *
 *     strstr / wcsstr          wcscpy / wcsncpy          strcpy / strcat
 *
 * So the question this file asks is simply: Was any of them left behind too? It times each pair on
 * identical work and reports ns per character for both, because the pair only tells you anything
 * when the two are measured the same way. A wide routine costing about the same per CHARACTER as
 * its narrow sibling is doing twice the bytes for the same money and is vectorised; one costing
 * twice as much per character is doing the same bytes for twice the money, which is what scalar
 * looks like.
 *
 * Also included: wcsstr against strstr is the pair most worth knowing, because change 089 already
 * measured `strstr` and PARKED it ("ties/loses below ~2 KB"). If `wcsstr` is scalar, the wide one
 * is a target even though the narrow one was not, which is precisely the 148/149 shape.
 *
 * Run on an idle machine, and not during a revalidation sweep. min-of-N.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static double bestns(void (*op)(void), int inner, int trials)
{
    LARGE_INTEGER f, a, b;
    double bv = 1e300;
    int t, i;
    QueryPerformanceFrequency(&f);
    for (i = 0; i < 64; ++i) op();
    for (t = 0; t < trials; ++t) {
        QueryPerformanceCounter(&a);
        for (i = 0; i < inner; ++i) op();
        QueryPerformanceCounter(&b);
        {
            double v = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / inner;
            if (v < bv) bv = v;
        }
    }
    return bv;
}

static volatile unsigned long long sink;

#define CAP 4200
static char    n1[CAP], n2[CAP], nneedle[64];
static wchar_t w1[CAP], w2[CAP], wneedle[64];
static int     g_len;

typedef char*    (__cdecl *F_sstr)(const char*, const char*);
typedef wchar_t* (__cdecl *F_wstr)(const wchar_t*, const wchar_t*);
typedef char*    (__cdecl *F_scpy)(char*, const char*);
typedef wchar_t* (__cdecl *F_wcpy)(wchar_t*, const wchar_t*);
typedef wchar_t* (__cdecl *F_wncpy)(wchar_t*, const wchar_t*, size_t);
typedef char*    (__cdecl *F_sncpy)(char*, const char*, size_t);
typedef char*    (__cdecl *F_scat)(char*, const char*);
typedef wchar_t* (__cdecl *F_wcat)(wchar_t*, const wchar_t*);
typedef char*    (__cdecl *F_srchr)(const char*, int);
typedef wchar_t* (__cdecl *F_wrchr)(const wchar_t*, wchar_t);

static F_sstr  f_strstr;   static F_wstr  f_wcsstr;
static F_scpy  f_strcpy;   static F_wcpy  f_wcscpy;
static F_sncpy f_strncpy;  static F_wncpy f_wcsncpy;
static F_scat  f_strcat;   static F_wcat  f_wcscat;
static F_srchr f_strrchr;  static F_wrchr f_wcsrchr;

static void o_strstr (void){ sink += (size_t)f_strstr(n1, nneedle); }
static void o_wcsstr (void){ sink += (size_t)f_wcsstr(w1, wneedle); }
static void o_strcpy (void){ sink += (size_t)f_strcpy(n2, n1); }
static void o_wcscpy (void){ sink += (size_t)f_wcscpy(w2, w1); }
static void o_strncpy(void){ sink += (size_t)f_strncpy(n2, n1, (size_t)g_len); }
static void o_wcsncpy(void){ sink += (size_t)f_wcsncpy(w2, w1, (size_t)g_len); }
static void o_strcat (void){ n2[0] = 0; sink += (size_t)f_strcat(n2, n1); }
static void o_wcscat (void){ w2[0] = 0; sink += (size_t)f_wcscat(w2, w1); }
static void o_strrchr(void){ sink += (size_t)f_strrchr(n1, '#'); }
static void o_wcsrchr(void){ sink += (size_t)f_wcsrchr(w1, L'#'); }

static void pair(const char* nname, void (*nop)(void),
                 const char* wname, void (*wop)(void))
{
    double a = bestns(nop, 2000, 30);
    double b = bestns(wop, 2000, 30);
    double pa = a / (double)g_len, pb = b / (double)g_len;
    const char* verdict;
    /* A vectorised wide routine does 2 bytes per character, so its ns/char should be close to the
       narrow one's -- not double it. Double is what a scalar per-element loop costs. */
    if (pb > pa * 1.7)      verdict = "*** the WIDE one looks SCALAR ***";
    else if (pb > pa * 1.25) verdict = "wide is slower per char";
    else                     verdict = "both vectorised (or both scalar)";
    printf("  %-9s %8.2f ns %7.3f ns/char   |   %-9s %8.2f ns %7.3f ns/char   %s\n",
           nname, a, pa, wname, b, pb, verdict);
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"ucrtbase.dll");
    static const int LENS[] = { 64, 254, 1024, 4000 };
    int li, k;

    f_strstr  = (F_sstr) GetProcAddress(h, "strstr");
    f_wcsstr  = (F_wstr) GetProcAddress(h, "wcsstr");
    f_strcpy  = (F_scpy) GetProcAddress(h, "strcpy");
    f_wcscpy  = (F_wcpy) GetProcAddress(h, "wcscpy");
    f_strncpy = (F_sncpy)GetProcAddress(h, "strncpy");
    f_wcsncpy = (F_wncpy)GetProcAddress(h, "wcsncpy");
    f_strcat  = (F_scat) GetProcAddress(h, "strcat");
    f_wcscat  = (F_wcat) GetProcAddress(h, "wcscat");
    f_strrchr = (F_srchr)GetProcAddress(h, "strrchr");
    f_wcsrchr = (F_wrchr)GetProcAddress(h, "wcsrchr");
    if (!f_strstr || !f_wcsstr || !f_strcpy || !f_wcscpy || !f_strncpy || !f_wcsncpy ||
        !f_strcat || !f_wcscat || !f_strrchr || !f_wcsrchr) {
        printf("could not resolve the whole set\n"); return 2;
    }

    printf("Narrow vs wide, measured the same way. A vectorised wide routine moves TWO bytes per\n"
           "character, so its ns/char should sit near its narrow sibling's; double means scalar.\n");
    printf("RUN ON AN IDLE MACHINE. min-of-30.\n\n");

    for (li = 0; li < 4; ++li) {
        g_len = LENS[li];
        for (k = 0; k < g_len; ++k) { n1[k] = (char)('a' + (k % 23)); w1[k] = (wchar_t)(L'a' + (k % 23)); }
        n1[g_len] = 0; w1[g_len] = 0;
        /* a miss for the searches: the needle is not present, so the whole subject is scanned */
        strcpy_s(nneedle, sizeof nneedle, "zzzq");
        wcscpy_s(wneedle, 64, L"zzzq");
        /* one '#' near the end for the reverse searches, so they cannot exit early */
        n1[g_len - 3] = '#'; w1[g_len - 3] = L'#';

        printf("---- %d characters ----\n", g_len);
        pair("strstr",  o_strstr,  "wcsstr",  o_wcsstr);
        pair("strcpy",  o_strcpy,  "wcscpy",  o_wcscpy);
        pair("strncpy", o_strncpy, "wcsncpy", o_wcsncpy);
        pair("strcat",  o_strcat,  "wcscat",  o_wcscat);
        pair("strrchr", o_strrchr, "wcsrchr", o_wcsrchr);   /* the KNOWN case, as a control */
        printf("\n");
    }

    printf("THE strrchr/wcsrchr ROW IS THE CONTROL. Change 149 already established that the wide one\n"
           "is scalar and landed against it at 1.82x, so that row must come out as 'the WIDE one looks\n"
           "SCALAR'. If it does not, this file is measuring the wrong thing and no other row here\n"
           "means anything -- which is the check that was missing when KEEP-AS-IS recorded the pair\n"
           "as one row in the first place.\n");
    return 0;
}
