/* changes/276-varbstrcmp/probes/gap.c
 *
 * IS THERE ROOM BETWEEN VarBstrCmp AND CompareStringW? THIS IS THE DECIDING NUMBER.
 *
 * What the first two probes settled:
 *
 *   * VarBstrCmp IS CompareStringW. Nine pairs where linguistic and ordinal collation disagree, and
 *     the export tracked the linguistic answer every time; the flag bits pass straight through and
 *     the result is CompareStringW's minus one. The collation is the OS's and is not reimplementable
 *     -- change 210's notes say the same.
 *   * IDENTICAL STRINGS ALWAYS COMPARE EQUAL. Every code unit 1..0xFFFF alone and inside a longer
 *     string, every surrogate and noncharacter asked, under every valid flag and locale: 0 of
 *     131070 not reflexive. The only non-EQ answers came from INVALID locales and flag bits, where
 *     the export returns an ERROR -- which a fast path must not turn into EQ.
 *   * And there is no fast path in the export at all: comparing a BSTR with ITSELF costs 3145 ns at
 *     4000 characters, and two equal strings cost 3224 ns, while a difference at character 0 costs a
 *     flat 33.5 ns at EVERY length.
 *
 * So a replacement would be: a lean wrapper that answers EQ when the operands are byte-identical and
 * otherwise calls CompareStringW. Whether that is faster than the shipped one depends entirely on
 * TWO gaps, and neither is guessable:
 *
 *   1. VarBstrCmp minus CompareStringW at the SAME length. That is the wrapper overhead, and it is
 *      what a lean wrapper competes against on the SHORT rows -- the rows that decide whether a
 *      change lands, as change 274 found out after the fact.
 *   2. the cost of a byte comparison against the cost of collating the same length. That is the win
 *      on the LONG rows.
 *
 * If gap 1 is near zero, the short rows cannot improve and this change is parked before it is
 * written -- which is the whole point of asking now.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "oleaut32.lib")

static double freq;
static volatile long long sink;

#define REP 400

static double t_var(BSTR a, BSTR b)
{
    LARGE_INTEGER x, y;
    double best = 1e300;
    int pass, i;
    for (pass = 0; pass < 150; ++pass) {
        QueryPerformanceCounter(&x);
        for (i = 0; i < REP; ++i) sink += VarBstrCmp(a, b, LOCALE_USER_DEFAULT, 0);
        QueryPerformanceCounter(&y);
        { double ns = (double)(y.QuadPart - x.QuadPart) * 1e9 / freq / REP;
          if (ns < best) best = ns; }
    }
    return best;
}
static double t_csw(const wchar_t* a, int na, const wchar_t* b, int nb)
{
    LARGE_INTEGER x, y;
    double best = 1e300;
    int pass, i;
    for (pass = 0; pass < 150; ++pass) {
        QueryPerformanceCounter(&x);
        for (i = 0; i < REP; ++i) sink += CompareStringW(LOCALE_USER_DEFAULT, 0, a, na, b, nb);
        QueryPerformanceCounter(&y);
        { double ns = (double)(y.QuadPart - x.QuadPart) * 1e9 / freq / REP;
          if (ns < best) best = ns; }
    }
    return best;
}
static double t_memcmp(const wchar_t* a, const wchar_t* b, int n)
{
    LARGE_INTEGER x, y;
    double best = 1e300;
    int pass, i;
    for (pass = 0; pass < 150; ++pass) {
        QueryPerformanceCounter(&x);
        for (i = 0; i < REP; ++i) sink += memcmp(a, b, (size_t)n * 2);
        QueryPerformanceCounter(&y);
        { double ns = (double)(y.QuadPart - x.QuadPart) * 1e9 / freq / REP;
          if (ns < best) best = ns; }
    }
    return best;
}

int main(void)
{
    LARGE_INTEGER f;
    static wchar_t p[8200], q[8200];
    static const int LENS[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 1000, 4000, 8000 };
    unsigned k;

    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&f); freq = (double)f.QuadPart;

    printf("== EQUAL strings: where the win would be ==\n");
    printf("   %6s %12s %12s %10s   %12s %10s\n",
           "chars", "VarBstrCmp", "CompareStrW", "wrapper", "memcmp", "best case");
    for (k = 0; k < sizeof LENS / sizeof LENS[0]; ++k) {
        int n = LENS[k], i;
        double tv, tc, tm;
        BSTR a, b;
        for (i = 0; i < n; ++i) { p[i] = (wchar_t)(L'a' + (i % 26)); q[i] = p[i]; }
        p[n] = q[n] = 0;
        a = SysAllocStringLen(p, (UINT)n);
        b = SysAllocStringLen(q, (UINT)n);
        tv = t_var(a, b);
        tc = t_csw(p, n, q, n);
        tm = t_memcmp(p, q, n);
        printf("   %6d %12.2f %12.2f %10.2f   %12.2f %10.2f\n", n, tv, tc, tv - tc, tm, tm);
        SysFreeString(a); SysFreeString(b);
    }

    printf("\n== DIFFERING strings (at character 0): where the short rows are decided ==\n");
    printf("   %6s %12s %12s %10s\n", "chars", "VarBstrCmp", "CompareStrW", "wrapper");
    for (k = 0; k < sizeof LENS / sizeof LENS[0]; ++k) {
        int n = LENS[k], i;
        double tv, tc;
        BSTR a, b;
        for (i = 0; i < n; ++i) { p[i] = (wchar_t)(L'a' + (i % 26)); q[i] = p[i]; }
        q[0] = L'Z';
        p[n] = q[n] = 0;
        a = SysAllocStringLen(p, (UINT)n);
        b = SysAllocStringLen(q, (UINT)n);
        tv = t_var(a, b);
        tc = t_csw(p, n, q, n);
        printf("   %6d %12.2f %12.2f %10.2f\n", n, tv, tc, tv - tc);
        SysFreeString(a); SysFreeString(b);
    }

    printf("\n== and the SAME POINTER twice, which nothing in the export notices ==\n");
    {
        int n = 4000, i;
        BSTR a;
        for (i = 0; i < n; ++i) p[i] = (wchar_t)(L'a' + (i % 26));
        p[n] = 0;
        a = SysAllocStringLen(p, (UINT)n);
        printf("   VarBstrCmp(x, x) at 4000 characters: %.2f ns\n", t_var(a, a));
        printf("   a pointer comparison costs nothing at all\n");
        SysFreeString(a);
    }

    printf("\n   READ THE 'wrapper' COLUMN. It is what the shipped VarBstrCmp adds on top of the\n"
           "   CompareStringW it is calling. A lean wrapper has to beat THAT, and only that, on\n"
           "   every row -- the long rows are won by the memcmp column instead.\n");
    printf("\nsink=%lld\n", (long long)sink);
    return 0;
}
