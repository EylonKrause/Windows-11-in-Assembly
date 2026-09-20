/* discovery/oleaut32_sysallocstring.c
 *
 * The one row in discovery/oleaut32_bstr.c that did not explain itself.
 *
 *     SysAllocString(src)        221-227 ns   for a 512-character source
 *     wcslen(src)                 12-16 ns
 *     SysAllocStringLen(src,256)  19.8 ns
 *
 * SysAllocString is documented as, and can only be, "measure the string, then do what
 * SysAllocStringLen does". Its two parts cost about 36 ns together and it costs 221. Either the
 * parts are not what it does, or there is 6x sitting in it.
 *
 * Before treating that as a target, three things have to be separated, and a single timing cannot
 * do it:
 *   1. does the cost scale with length? If it does, it is a loop and the loop is replaceable. If it
 *      is flat, it is a fixed cost -- a lock, a TLS lookup, a cache miss on a global -- and no
 *      assembly removes it.
 *   2. is the ALLOCATION ITSELF bigger here? SysAllocString allocates len+1 characters; the
 *      comparison row allocated a different size. Sizes are matched exactly below.
 *   3. is it the FIRST call that is expensive -- a one-time initialisation the min-of-N would
 *      normally hide but a per-call fixed cost would not?
 *
 * The answer decides whether oleaut32 gets its first change or its first recorded negative result.
 * Both are worth having; guessing which is not.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>

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

#define MAXW 4096
static WCHAR src[MAXW + 8];
static int   g_len;

/* the subject */
static void op_sas(void)      { BSTR b = SysAllocString(src);                    sink += (size_t)b; SysFreeString(b); }
/* the same allocation, the same copy, but the length handed in rather than measured */
static void op_sasl(void)     { BSTR b = SysAllocStringLen(src, (UINT)g_len);    sink += (size_t)b; SysFreeString(b); }
/* the allocation alone, at the SAME size */
static void op_alloc(void)    { BSTR b = SysAllocStringLen(NULL, (UINT)g_len);   sink += (size_t)b; SysFreeString(b); }
/* the measurement alone */
static void op_wcslen(void)   { sink += wcslen(src); }

int main(void)
{
    int i;
    static const int LENS[] = { 0, 1, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
    int n = (int)(sizeof LENS / sizeof LENS[0]);
    int k;

    for (i = 0; i < MAXW; ++i) src[i] = (WCHAR)(L'a' + (i % 26));

    printf("SysAllocString vs its two parts, at matched allocation sizes.\n");
    printf("RUN THIS ON AN IDLE MACHINE. min-of-40. Every row states the length it produced.\n\n");
    printf("  %-6s %11s %11s %11s %11s   %10s %s\n",
           "len", "SysAlloc", "SysAllocLen", "alloc only", "wcslen", "SAS/parts", "produced");
    printf("  ---------------------------------------------------------------------------------------\n");

    for (k = 0; k < n; ++k) {
        double a, b, c, d;
        UINT produced;
        g_len = LENS[k];
        src[g_len] = 0;                       /* terminate exactly where the length says */

        { BSTR t = SysAllocString(src); produced = t ? SysStringLen(t) : 0xFFFFFFFF; SysFreeString(t); }

        a = bestns(op_sas,   2000, 40);
        b = bestns(op_sasl,  2000, 40);
        c = bestns(op_alloc, 2000, 40);
        d = bestns(op_wcslen,2000, 40);

        printf("  %-6d %10.2f %11.2f %11.2f %11.2f   %9.2fx  len=%u\n",
               g_len, a, b, c, d, (b + d) > 0 ? a / (b + d) : 0.0, produced);

        src[g_len] = (WCHAR)(L'a' + (g_len % 26));   /* restore for the next length */
    }

    printf("\nREAD THE 'SAS/parts' COLUMN. It is SysAllocString divided by (SysAllocStringLen + wcslen)\n"
           "at the SAME length -- what the function costs against what its own two documented parts\n"
           "cost. A ratio near 1 means there is nothing in it. A ratio that GROWS with length means a\n"
           "loop worth replacing. A ratio that is large but FLAT means a fixed per-call cost, which is\n"
           "not a byte loop and not a target for this repository.\n");
    return 0;
}
