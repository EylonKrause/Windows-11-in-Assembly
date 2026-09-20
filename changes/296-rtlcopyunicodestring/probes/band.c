/* changes/296-rtlcopyunicodestring/probes/band.c
 *
 * THROWAWAY. twoaxes.c established that `rep movsb` collapses when the source and destination
 * share a page phase -- (dst - src) mod 4096 near zero -- and is 2x-2.4x faster than the live
 * export when they do not. If that band is narrow and sharp, the ERMS arm can simply step around
 * it with a three-instruction test, and the implementation gets the best of both. If it is broad
 * or ragged, it cannot, and the honest answer is that the arm has an irreducible bad regime.
 *
 * So: sweep (dst - src) mod 4096 across the whole period at n = 8190, with NO overlap, and time
 * rep movsb, the vector loop and the live export at each point.
 *
 *   BUILD
 *     . .\tools\vsenv.ps1
 *     cl /nologo /O2 band.c X.obj /Fe:band.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } US;
extern void wia_copyus(US*, const US*);
extern unsigned wia_erms_min;
typedef void (NTAPI *fn)(US*, const US*);
static fn sys;
static volatile uint64_t sink;
static double freq;
static double ns(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart * 1e9 / freq; }

static double nsper(int useours, unsigned char* d, unsigned char* s, int n, int reps)
{
    US src; src.Length = (USHORT)n; src.MaximumLength = (USHORT)n; src.Buffer = (wchar_t*)s;
    double best = 1e30;
    for (int t = 0; t < 15; ++t) {
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

int main(void)
{
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); freq = (double)f.QuadPart;
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    sys = (fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlCopyUnicodeString");
    unsigned char* a = (unsigned char*)VirtualAlloc(NULL, 1 << 21, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    for (int i = 0; i < (1 << 21); ++i) a[i] = (unsigned char)i;

    const int n = 8190, reps = 1200;
    /* dst fixed and 64-aligned; the SOURCE moves, so the destination's own alignment is constant
       and the only thing changing is the page phase between them. */
    unsigned char* d = a + 0x100000;
    printf("n=%d, dst fixed 64-aligned, src = dst - 0x80000 + p.  (dst-src)%%4096 = (0x80000 - p)%%4096\n", n);
    printf("%8s %8s | %9s %9s %9s | %8s %8s\n", "p", "phase", "erms ns", "vec ns", "ntdll ns", "erms/N", "vec/N");
    for (int p = 0; p < 4096; p += 128) {
        unsigned char* s = d - 0x80000 + p;
        int phase = (int)(((size_t)d - (size_t)s) & 4095);
        wia_erms_min = 64;          double e = nsper(1, d, s, n, reps);
        wia_erms_min = 0x7fffffff;  double v = nsper(1, d, s, n, reps);
        double t = nsper(0, d, s, n, reps);
        printf("%8d %8d | %9.2f %9.2f %9.2f | %7.2fx %7.2fx\n", p, phase, e, v, t, t / e, t / v);
    }
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
