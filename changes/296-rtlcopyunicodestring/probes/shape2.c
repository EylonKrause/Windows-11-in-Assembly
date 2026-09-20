/* changes/296-rtlcopyunicodestring/probes/shape2.c
 *
 * THROWAWAY, and the one that settled it. shape.c's "distance" axis was contaminated: every
 * distance it tried below n put the destination INSIDE the source, which routes this
 * implementation onto its backward (overlap) loop, so that sweep measured the path switch and
 * not the distance.
 *
 * This one sweeps the distance with NO overlap at all (D >= n), finely, which isolates the one
 * remaining suspect: 4K ALIASING. A load at src+i is stalled behind an in-flight store at
 * dst+j whenever the two share their low 12 address bits. In a copy loop the stores that are
 * still in flight are one block behind the current load, so the hazard is a property of
 *
 *     (dst - src - blocksize) mod 4096
 *
 * being small, i.e. of the distance between the caller's two buffers, which neither
 * implementation controls and neither can fix. If that is what the benchmark's malloc layout
 * kept landing on, then the right answer is to say so with numbers, not to move the
 * benchmark's buffers until the ratio looks better.
 *
 *   BUILD
 *     . .\tools\vsenv.ps1
 *     ml64 /nologo /c /Fo impl_p.obj changes\296-rtlcopyunicodestring\impl.asm
 *     cl /nologo /O2 changes\296-rtlcopyunicodestring\probes\shape2.c impl_p.obj /Fe:shape2.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } US;
extern void wia_copyus(US*, const US*);
typedef void (NTAPI *fn)(US*, const US*);
static fn sys;
static volatile uint64_t sink;
static double freq;
static double ns(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart * 1e9 / freq; }

static double gbs(int useours, unsigned char* d, unsigned char* s, int n)
{
    US src; src.Length = (USHORT)n; src.MaximumLength = (USHORT)n; src.Buffer = (wchar_t*)s;
    double best = 1e30;
    for (int t = 0; t < 40; ++t) {
        double t0 = ns();
        for (int r = 0; r < 400; ++r) {
            US u; u.Length = 0; u.MaximumLength = (USHORT)(n + 2); u.Buffer = (wchar_t*)d;
            if (useours) wia_copyus(&u, &src); else sys(&u, &src);
            sink += u.Length;
        }
        double dt = (ns() - t0) / 400.0;
        if (dt < best) best = dt;
    }
    return (double)n / best;
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

    printf("n=8190, NON-OVERLAPPING, src page-aligned, dst = src + D.  GB/s\n");
    printf("%8s %8s %8s %8s\n", "D", "D%4096", "ours", "ntdll");
    for (int D = 8192; D <= 8192 + 384; D += 16)
        printf("%8d %8d %8.1f %8.1f\n", D, D & 4095, gbs(1, a + D, a, 8190), gbs(0, a + D, a, 8190));
    for (int D = 12288; D <= 12288 + 128; D += 16)
        printf("%8d %8d %8.1f %8.1f\n", D, D & 4095, gbs(1, a + D, a, 8190), gbs(0, a + D, a, 8190));

    printf("\nn=2048, NON-OVERLAPPING.  GB/s\n");
    printf("%8s %8s %8s %8s\n", "D", "D%4096", "ours", "ntdll");
    for (int D = 2048; D <= 2048 + 384; D += 16)
        printf("%8d %8d %8.1f %8.1f\n", D, D & 4095, gbs(1, a + D, a, 2048), gbs(0, a + D, a, 2048));

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
