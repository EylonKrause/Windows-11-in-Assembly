/* changes/296-rtlcopyunicodestring/probes/detail.c
 *
 * THROWAWAY. crossover.c reduced each length to worst/median/best over 32 destination offsets and
 * showed the vector path's WORST offset sitting at 0.88x-0.99x at 64, 128, 508, 1536 and 2048
 * bytes. That number decides between LANDS and PARKED, so it has to be looked at rather than
 * trusted: a systematic alignment class is one thing, and a noisy tail of 32 samples is another.
 *
 * This prints the whole per-offset table (our ns, ntdll's ns, and the ratio) so the shape is
 * visible. If the low ratios cluster on particular offsets mod 16 or mod 32, they are real and
 * they are ours. If they scatter, or if they coincide with ntdll getting unusually FAST rather
 * than us getting slow, they are the comparand moving.
 *
 *   BUILD
 *     . .\tools\vsenv.ps1
 *     cl /nologo /O2 detail.c X.obj /Fe:detail.exe
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

    static const int NS[] = { 64, 128, 508, 1536, 2048 };
    wia_erms_min = 0x7fffffff;                       /* vector path only -- that is what is in doubt */
    for (int q = 0; q < 5; ++q) {
        int n = NS[q];
        int reps = n > 1024 ? 3000 : 8000;
        printf("\nn=%d  (vector path forced; src page-aligned; dst = src + 0x40000 + off)\n", n);
        printf("%5s %8s %8s %8s   %5s %8s %8s %8s\n", "off", "ours", "ntdll", "ratio", "off", "ours", "ntdll", "ratio");
        for (int k = 0; k < 16; ++k) {
            int o1 = 2 * k, o2 = 2 * k + 32;
            double v1 = nsper(1, a + 0x40000 + o1, a, n, reps), s1 = nsper(0, a + 0x40000 + o1, a, n, reps);
            double v2 = nsper(1, a + 0x40000 + o2, a, n, reps), s2 = nsper(0, a + 0x40000 + o2, a, n, reps);
            printf("%5d %8.2f %8.2f %7.2fx   %5d %8.2f %8.2f %7.2fx\n",
                   o1, v1, s1, s1 / v1, o2, v2, s2, s2 / v2);
        }
    }
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
