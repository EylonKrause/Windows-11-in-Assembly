/* changes/296-rtlcopyunicodestring/probes/crossover.c
 *
 * THROWAWAY, and the one that chose the threshold.
 *
 * WHY AN END-TO-END BENCH COULD NOT CHOOSE IT. bench.c gets its buffers from malloc, so
 * (dst - src) mod 32 is whatever that executable's heap happened to produce -- and that single
 * bit decides whether the 32-byte vector loop runs at ~95 GB/s or ~60. Rebuilding with a
 * different threshold relinks the executable, moves the heap, and changes the answer for size
 * classes that do not even reach the new threshold: across six builds the 128-wchar class, which
 * runs identical code in five of them, read 5.00, 6.14, 6.30, 6.34, 6.66 and 7.41 ns. That is not
 * a measurement of a threshold.
 *
 * So measure the thing directly. For every destination offset k (in WCHARS -- a PWSTR is at least
 * 2-aligned and no real UNICODE_STRING is odd) against a page-aligned source, time all three
 * AT THAT SAME OFFSET and form the ratio there:
 *      [V] the AVX2 vector loop     (threshold forced above the length)
 *      [E] rep movsb                (threshold forced to 64)
 *      [N] the live ntdll export
 * then report the WORST, MEDIAN and BEST of ratio(k) over k. Comparing our worst offset against
 * ntdll's best -- which an earlier version of this file did -- is not a comparison: ntdll's own
 * spread across these offsets is itself nearly 3x, so that metric mostly measures ntdll.
 *
 * THE WORST COLUMN IS THE ONE THAT DECIDES. The speed gate is a promise about every caller, not
 * about the lucky ones.
 *
 *   BUILD
 *     . .\tools\vsenv.ps1
 *     ml64 /nologo /c /Fo X.obj X.asm        (impl.asm with the threshold moved into a variable)
 *     cl /nologo /O2 crossover.c X.obj /Fe:crossover.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } US;
extern void wia_copyus(US*, const US*);
extern unsigned wia_erms_min;           /* the switchable threshold, in bytes */
typedef void (NTAPI *fn)(US*, const US*);
static fn sys;
static volatile uint64_t sink;
static double freq;
static double ns(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart * 1e9 / freq; }

static double nsper(int useours, unsigned char* d, unsigned char* s, int n, int reps)
{
    US src; src.Length = (USHORT)n; src.MaximumLength = (USHORT)n; src.Buffer = (wchar_t*)s;
    double best = 1e30;
    for (int t = 0; t < 21; ++t) {
        double t0 = ns();
        for (int r = 0; r < reps; ++r) {
            US u; u.Length = 0; u.MaximumLength = (USHORT)(n + 2); u.Buffer = (wchar_t*)d;
            if (useours) wia_copyus(&u, &src); else sys(&u, &src);
            sink += u.Length;
        }
        double dt = (ns() - t0) / reps;
        if (dt < best) best = dt;
    }
    return best;
}

static int cmpd(const void* a, const void* b)
{ double x = *(const double*)a, y = *(const double*)b; return x < y ? -1 : x > y; }

int main(void)
{
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); freq = (double)f.QuadPart;
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    sys = (fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlCopyUnicodeString");
    unsigned char* a = (unsigned char*)VirtualAlloc(NULL, 1 << 21, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    for (int i = 0; i < (1 << 21); ++i) a[i] = (unsigned char)i;

    static const int NS[] = { 64, 128, 256, 508, 1024, 1536, 2048, 2560, 3072, 4096, 6144, 8190 };
    enum { KOFF = 32 };                        /* dst offsets 0,2,4,...,62 wchars-aligned */
    static double rv[KOFF], re[KOFF];

    printf("ratio vs live ntdll, computed AT EACH destination offset (0,2,..,62), then ordered.\n");
    printf("%6s | %-24s | %-24s | %s\n", "n", "AVX2 vector loop", "rep movsb (ERMS)", "pick");
    printf("%6s | %7s %7s %7s | %7s %7s %7s |\n", "", "worst", "median", "best", "worst", "median", "best");
    for (int q = 0; q < (int)(sizeof NS / sizeof NS[0]); ++q) {
        int n = NS[q];
        int reps = n > 4096 ? 1200 : (n > 1024 ? 2500 : 6000);
        for (int k = 0; k < KOFF; ++k) {
            unsigned char* d = a + 0x40000 + 2 * k;
            wia_erms_min = 0x7fffffff;  double v = nsper(1, d, a, n, reps);
            wia_erms_min = 64;          double e = nsper(1, d, a, n, reps);
            double s = nsper(0, d, a, n, reps);
            rv[k] = s / v; re[k] = s / e;
        }
        qsort(rv, KOFF, sizeof(double), cmpd);
        qsort(re, KOFF, sizeof(double), cmpd);
        const char* pick = rv[0] >= re[0] ? "vector" : "ERMS";
        printf("%6d | %6.2fx %6.2fx %6.2fx | %6.2fx %6.2fx %6.2fx | %s\n",
               n, rv[0], rv[KOFF / 2], rv[KOFF - 1], re[0], re[KOFF / 2], re[KOFF - 1], pick);
    }
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
