/* changes/296-rtlcopyunicodestring/probes/twoaxes.c
 *
 * THROWAWAY. detail.c showed the vector path at n = 2048 taking ~26 ns for destination offsets
 * 0..30 and ~38 ns for 32..62, with the destination's 32-alignment identical in both halves. That
 * is not a cache-line-split effect, so a second mechanism is in play, and the two have to be
 * separated before any verdict is written down:
 *
 *   (a) CACHE-LINE SPLITS      - governed by dst & 31. Fixed in impl.asm by aligning the stores.
 *   (b) 4K ALIASING            - governed by (dst - src) mod 4096. A load is stalled behind an
 *                                in-flight store that shares its low 12 address bits. NOT fixable
 *                                in the implementation, and it hits a 32-byte loop harder than
 *                                ntdll's 16-byte one because the alias window is as wide as the
 *                                access.
 *
 * detail.c could not tell them apart because both its buffers were page-aligned, which forces
 * (dst - src) mod 4096 to equal the destination offset; the two axes were the same number.
 * Here the source is moved to a page offset of 2048, so the distance stays ~2 KB away from a
 * multiple of 4096 while the destination alignment still sweeps the full 64-byte line.
 *
 *   BUILD
 *     . .\tools\vsenv.ps1
 *     cl /nologo /O2 twoaxes.c X.obj /Fe:twoaxes.exe
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
    for (int t = 0; t < 31; ++t) {
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

    static const int NS[] = { 2048, 8190 };
    for (int q = 0; q < 2; ++q) {
        int n = NS[q];
        int reps = n > 4096 ? 1500 : 4000;
        for (int srcpo = 0; srcpo <= 2048; srcpo += 2048) {     /* source page offset */
            unsigned char* s = a + srcpo;
            printf("\nn=%d  src page offset %d  (so (dst-src)%%4096 = off %+d)\n",
                   n, srcpo, -srcpo);
            printf("%5s %9s %9s %8s | %9s %8s\n", "off", "vec ns", "ntdll ns", "vec/N", "erms ns", "erms/N");
            double wv = 1e9, we = 1e9;
            for (int off = 0; off < 64; off += 4) {
                unsigned char* d = a + 0x40000 + off;
                wia_erms_min = 0x7fffffff; double v = nsper(1, d, s, n, reps);
                wia_erms_min = 64;         double e = nsper(1, d, s, n, reps);
                double t = nsper(0, d, s, n, reps);
                if (t / v < wv) wv = t / v;
                if (t / e < we) we = t / e;
                printf("%5d %9.2f %9.2f %7.2fx | %9.2f %7.2fx\n", off, v, t, t / v, e, t / e);
            }
            printf("  WORST ratio over these offsets:  vector %.2fx   ERMS %.2fx\n", wv, we);
        }
    }
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
