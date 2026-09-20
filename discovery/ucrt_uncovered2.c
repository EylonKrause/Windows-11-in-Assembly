/* discovery/ucrt_uncovered2.c
 *
 * ucrtbase carries 75 landed changes and STILL has uncovered exports that are plainly byte loops.
 * An earlier sweep took the obvious ones -- strlen, strcmp, wcschr, the _s-suffixed copies, the
 * integer conversions -- and left behind a set that this file measures properly rather than
 * guessing at. The whole uncovered list was produced mechanically (enumerate ucrtbase's exports,
 * subtract image/tree's filenames, drop the _o__ ordinal aliases, the _l locale variants and the
 * _mbs code-page family) and this measures every survivor with a pinnable contract.
 *
 * What is deliberately not here:
 *   * the *coll / *xfrm family (strcoll, wcsxfrm, _stricoll, _wcsncoll ...) -- that is COLLATION,
 *     the same category that puts StrCmpLogicalW, StrChrIW and StrStrIW out of reach. Locale sort
 *     order cannot be reproduced bit-exactly from an ordinal table, and this project has already
 *     established that four separate times.
 *   * the _mbs* family and mbstowcs/wcstombs -- their behaviour depends on the process code page,
 *     so the contract is machine state rather than a specification.
 *   * setlocale, _tzset, the _find* file walkers, the stdio and exception machinery -- those are
 *     not byte loops at all.
 *   * memcpy/memmove/memset/memcmp, which are the one part of the CRT Microsoft certainly did
 *     vectorise. They are measured ANYWAY, in one row each, because "already optimal" is a claim
 *     and the point of a survey is to check claims rather than repeat them.
 *
 * Method, and the mistake this file is written to avoid: Every row prints what it actually did --
 * the return value, and where there is one, the output. A survey row whose subject does not do the
 * work its label claims is this project's most expensive recurring mistake, and the previous ntdll
 * survey shipped TWO of them (a comparison handed byte counts where it wanted characters, and a
 * bitmap copy called with three arguments where it takes four); both were invisible in the timing
 * and obvious in the returned value.
 *
 * Run it on an idle machine. Every row is a min-of-60.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

#define NW 4000

static char    a1[NW + 64], a2[NW + 64], a3[NW + 64];
static wchar_t w1[NW + 64], w2[NW + 64];
static char    needle_a[64], abuf[NW + 64];
static wchar_t needle_w[64], wbuf[NW + 64];

/* A REALISTIC PATH, and the reason it has to be realistic. _splitpath has no size parameters: it
   assumes _MAX_DRIVE/_MAX_DIR/_MAX_FNAME/_MAX_EXT and hands anything longer to the invalid-parameter
   handler WITHOUT writing an output. Handed the 4000-character subject the other rows use, it
   measured 11852 ns and produced an EMPTY filename -- an error path timed as though it were a
   split. Caught only because the row printed the length it produced. */
static char    pth[320];
static wchar_t wpth[320];

static double bestns(void (*op)(void), int inner, int trials)
{
    LARGE_INTEGER f, x, y;
    double bv = 1e300;
    int t, i;
    QueryPerformanceFrequency(&f);
    for (i = 0; i < 64; ++i) op();
    for (t = 0; t < trials; ++t) {
        QueryPerformanceCounter(&x);
        for (i = 0; i < inner; ++i) op();
        QueryPerformanceCounter(&y);
        {
            double v = (double)(y.QuadPart - x.QuadPart) * 1e9 / (double)f.QuadPart / inner;
            if (v < bv) bv = v;
        }
    }
    return bv;
}

static volatile unsigned long long sink;

/* ---- the subjects ------------------------------------------------------------------------- */
static void op_strstr(void)   { sink += (size_t)strstr(a1, needle_a); }
static void op_wcsstr(void)   { sink += (size_t)wcsstr(w1, needle_w); }
static void op_strstr_hit(void){ sink += (size_t)strstr(a2, needle_a); }
static void op_wcsstr_hit(void){ sink += (size_t)wcsstr(w2, needle_w); }
static void op_strchr(void)   { sink += (size_t)strchr(a1, '#'); }
static void op_strrchr(void)  { sink += (size_t)strrchr(a1, '#'); }
static void op_strnlen(void)  { sink += strnlen(a1, NW + 64); }
static void op_wcsnlen(void)  { sink += wcsnlen(w1, NW + 64); }
/* a3, not a2: a2 has the needle planted in it at offset 3000, so comparing against it stopped
   there and the row was labelled "equal" while returning -1. a3 is a true copy. */
static void op_strncmp(void)  { sink += (unsigned)strncmp(a1, a3, NW); }
static void op_strcpy(void)   { strcpy(abuf, a1); sink += abuf[0]; }
static void op_wcscpy(void)   { wcscpy(wbuf, w1); sink += wbuf[0]; }
static void op_strncpy(void)  { strncpy(abuf, a1, NW); sink += abuf[0]; }
static void op_strcat(void)   { abuf[0] = 0; strcat(abuf, a1); sink += abuf[0]; }
static void op_strncat(void)  { abuf[0] = 0; strncat(abuf, a1, NW); sink += abuf[0]; }
static void op_wcscat(void)   { wbuf[0] = 0; wcscat(wbuf, w1); sink += wbuf[0]; }
static void op_memcmp(void)   { sink += (unsigned)memcmp(a1, a3, NW); }
static void op_memcpy(void)   { memcpy(abuf, a1, NW); sink += abuf[0]; }
static void op_memset(void)   { memset(abuf, 'q', NW); sink += abuf[0]; }
static void op_memmove(void)  { memmove(abuf, a1, NW); sink += abuf[0]; }

static char  dr[8], di[NW + 8], fn[NW + 8], ex[NW + 8];
static wchar_t wdr[8], wdi[NW + 8], wfn[NW + 8], wex[NW + 8];
static void op_splitpath(void) { _splitpath(pth, dr, di, fn, ex); sink += (unsigned char)fn[0]; }
static void op_wsplitpath(void){ _wsplitpath(wpth, wdr, wdi, wfn, wex); sink += wfn[0]; }
static void op_makepath(void)  { _makepath(abuf, dr, di, fn, ex); sink += (unsigned char)abuf[0]; }

static void op_strtoll(void)  { sink += (unsigned)strtoll("9223372036854775807", 0, 10); }
static void op_strtod(void)   { sink += (unsigned)strtod("3.14159265358979311599796346854", 0); }
static void op_wcstod(void)   { sink += (unsigned)wcstod(L"3.14159265358979311599796346854", 0); }

static int cmpint(const void* p, const void* q) { return *(const int*)p - *(const int*)q; }
static int sorted[4096];
static void op_bsearch(void)  { int k = 3999; sink += (size_t)bsearch(&k, sorted, 4096, sizeof(int), cmpint); }

int main(void)
{
    int i;
    char* r;

    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    /* a1/w1: 4000 characters over 26 letters, no '#' anywhere -> strchr/strrchr scan it all */
    for (i = 0; i < NW; ++i) { a1[i] = (char)('a' + i % 26); w1[i] = (wchar_t)(L'a' + i % 26); }
    a1[NW] = 0; w1[NW] = 0;
    memcpy(a2, a1, NW + 1);
    memcpy(w2, w1, sizeof(wchar_t) * (NW + 1));
    /* the needle is ABSENT from a1/w1 (a run of 'z' never occurs in a 26-cycle) -> a FULL scan */
    strcpy(needle_a, "zzzzzzzz");
    wcscpy(needle_w, L"zzzzzzzz");
    /* ... and PRESENT near the end of a2/w2, so the hit rows are not measuring a two-character
       scan by accident */
    memcpy(a2 + 3000, needle_a, 8);
    memcpy(w2 + 3000, needle_w, sizeof(wchar_t) * 8);
    memcpy(a3, a1, NW + 1);            /* a TRUE copy, for the comparison rows */
    for (i = 0; i < 4096; ++i) sorted[i] = i;

    /* a path with components INSIDE the legacy limits -- see the note on pth[] */
    strcpy(pth, "C:\\Program Files\\Some Vendor\\Some Product\\bin\\x64\\release\\component");
    for (i = 0; i < 10; ++i) strcat(pth, "\\sub");
    strcat(pth, "\\module.library.extension");
    wcscpy(wpth, L"C:\\Program Files\\Some Vendor\\Some Product\\bin\\x64\\release\\component");
    for (i = 0; i < 10; ++i) wcscat(wpth, L"\\sub");
    wcscat(wpth, L"\\module.library.extension");
    _splitpath(pth, dr, di, fn, ex);   /* so _makepath has real components to assemble */

    printf("ucrtbase -- the uncovered exports that are plainly byte loops\n");
    printf("RUN THIS ON AN IDLE MACHINE. Every row is a min-of-60 and states what it did.\n\n");
    printf("  %-32s %12s %10s  %s\n", "export / subject", "ns", "ns/byte", "what it returned");

#define ROW(label, bytes, act) do {                                                            \
        double t = bestns(act, 20000, 60);                                                     \
        printf("  %-32s %12.2f %10.3f  ", label, t,                                            \
               (double)(bytes) ? t / (double)(bytes) : 0.0);                                   \
    } while (0)

    ROW("strstr, 4000 B, MISS", NW, op_strstr);
    printf("found=%p (NULL = scanned it all)\n", (void*)strstr(a1, needle_a));
    ROW("wcsstr, 4000 ch, MISS", NW * 2, op_wcsstr);
    printf("found=%p\n", (void*)wcsstr(w1, needle_w));
    ROW("strstr, hit at 3000", NW, op_strstr_hit);
    printf("offset=%d\n", (int)(strstr(a2, needle_a) - a2));
    ROW("wcsstr, hit at 3000", NW * 2, op_wcsstr_hit);
    printf("offset=%d\n", (int)(wcsstr(w2, needle_w) - w2));
    ROW("strchr, 4000 B, absent", NW, op_strchr);
    printf("found=%p\n", (void*)strchr(a1, '#'));
    ROW("strrchr, 4000 B, absent", NW, op_strrchr);
    printf("found=%p\n", (void*)strrchr(a1, '#'));
    ROW("strnlen, 4000 B", NW, op_strnlen);
    printf("len=%zu\n", strnlen(a1, NW + 64));
    ROW("wcsnlen, 4000 ch", NW * 2, op_wcsnlen);
    printf("len=%zu\n", wcsnlen(w1, NW + 64));
    ROW("strncmp, 4000 B, equal", NW, op_strncmp);
    printf("cmp=%d (0 = equal, i.e. a FULL scan)\n", strncmp(a1, a3, NW));
    ROW("strcpy, 4000 B", NW, op_strcpy);
    printf("len=%zu\n", strlen(abuf));
    ROW("wcscpy, 4000 ch", NW * 2, op_wcscpy);
    printf("len=%zu\n", wcslen(wbuf));
    ROW("strncpy, 4000 B", NW, op_strncpy);
    printf("first=%c\n", abuf[0]);
    ROW("strcat, onto empty, 4000 B", NW, op_strcat);
    printf("len=%zu\n", strlen(abuf));
    ROW("strncat, onto empty, 4000 B", NW, op_strncat);
    printf("len=%zu\n", strlen(abuf));
    ROW("wcscat, onto empty, 4000 ch", NW * 2, op_wcscat);
    printf("len=%zu\n", wcslen(wbuf));
    ROW("_splitpath, 121-B path", (int)strlen(pth), op_splitpath);
    { _splitpath(pth, dr, di, fn, ex);
      printf("dir len=%zu fname=\"%s\" ext=\"%s\"\n", strlen(di), fn, ex); }
    ROW("_wsplitpath, 121-ch path", (int)wcslen(wpth) * 2, op_wsplitpath);
    { _wsplitpath(wpth, wdr, wdi, wfn, wex);
      printf("dir len=%zu fname=\"%ls\"\n", wcslen(wdi), wfn); }
    ROW("_makepath, same components", (int)strlen(pth), op_makepath);
    printf("len=%zu\n", strlen(abuf));
    ROW("strtoll, 19 digits", 19, op_strtoll);
    printf("val=%lld\n", strtoll("9223372036854775807", 0, 10));
    ROW("strtod, 31 chars", 31, op_strtod);
    printf("val=%.17g\n", strtod("3.14159265358979311599796346854", 0));
    ROW("wcstod, 31 chars", 62, op_wcstod);
    printf("val=%.17g\n", wcstod(L"3.14159265358979311599796346854", 0));
    ROW("bsearch, 4096 ints", 12, op_bsearch);
    { int k = 3999; printf("found=%p\n", bsearch(&k, sorted, 4096, sizeof(int), cmpint)); }

    printf("\n  -- the four Microsoft certainly DID vectorise, measured anyway --\n");
    ROW("memcmp, 4000 B, equal", NW, op_memcmp);
    printf("cmp=%d (0 = equal, i.e. a FULL scan)\n", memcmp(a1, a3, NW));
    ROW("memcpy, 4000 B", NW, op_memcpy);
    printf("first=%c\n", abuf[0]);
    ROW("memmove, 4000 B", NW, op_memmove);
    printf("first=%c\n", abuf[0]);
    ROW("memset, 4000 B", NW, op_memset);
    printf("first=%c\n", abuf[0]);

    (void)r;
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
